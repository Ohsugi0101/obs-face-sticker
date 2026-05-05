/*
Face Sticker Plugin for OBS Studio
Copyright (C) 2026 Manus
SPDX-License-Identifier: GPL-2.0-or-later

This filter detects faces (using OpenCV YuNet) on the incoming video frames
of a source and overlays a user-specified PNG image (e.g. sunglasses)
onto each detected face, following its position, scale and orientation
in real time.
*/

#include <obs-module.h>
#include <plugin-support.h>
#include "face-sticker-filter.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/objdetect.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Settings keys (saved in scene collection).
// ---------------------------------------------------------------------------
constexpr const char *kSettingImagePath = "image_path";
constexpr const char *kSettingAttach = "attach_to";
constexpr const char *kSettingScale = "scale";
constexpr const char *kSettingOffsetY = "offset_y";
constexpr const char *kSettingFollowRotation = "follow_rotation";
constexpr const char *kSettingDetectInterval = "detect_interval";
constexpr const char *kSettingSmoothing = "smoothing";
constexpr const char *kSettingMaxFaces = "max_faces";
constexpr const char *kSettingScoreThreshold = "score_threshold";

// Attachment modes.
enum class AttachMode : int {
	Eyes = 0,    // sunglasses-like: between eyes, oriented along eye line
	FullFace = 1, // mask-like: covers whole bbox
	Mouth = 2,   // moustache-like: anchored on mouth
	Forehead = 3 // hat-like: above the eyes
};

// One detected face (after smoothing).
struct FaceState {
	float bbox_x = 0, bbox_y = 0, bbox_w = 0, bbox_h = 0;
	float right_eye_x = 0, right_eye_y = 0;
	float left_eye_x = 0, left_eye_y = 0;
	float nose_x = 0, nose_y = 0;
	float right_mouth_x = 0, right_mouth_y = 0;
	float left_mouth_x = 0, left_mouth_y = 0;
	bool valid = false;
};

// ---------------------------------------------------------------------------
// Filter data.
// ---------------------------------------------------------------------------
struct FaceStickerFilter {
	obs_source_t *self = nullptr;

	// Settings.
	std::string image_path;
	AttachMode attach_mode = AttachMode::Eyes;
	float scale = 1.0f;
	float offset_y = 0.0f; // in units of face width
	bool follow_rotation = true;
	int detect_interval = 2; // detect every N frames
	float smoothing = 0.6f;  // 0 = no smoothing, ~0.9 = very smooth
	int max_faces = 4;
	float score_threshold = 0.7f;

	// Sticker image (BGRA, premultiplied false). Loaded asynchronously safe.
	std::mutex sticker_mutex;
	cv::Mat sticker_bgra; // CV_8UC4

	// Detector.
	cv::Ptr<cv::FaceDetectorYN> detector;
	int detector_input_w = 0;
	int detector_input_h = 0;
	std::string model_path;

	// Face tracking state.
	std::vector<FaceState> faces;
	int frame_counter = 0;
	std::atomic<bool> active{true};
};

// ---------------------------------------------------------------------------
// Helpers: load PNG sticker (with alpha) into BGRA Mat.
// ---------------------------------------------------------------------------
bool load_sticker(FaceStickerFilter *f, const std::string &path)
{
	if (path.empty()) {
		std::lock_guard<std::mutex> lock(f->sticker_mutex);
		f->sticker_bgra.release();
		return true;
	}
	cv::Mat img = cv::imread(path, cv::IMREAD_UNCHANGED);
	if (img.empty()) {
		obs_log(LOG_WARNING, "Face Sticker: failed to load image: %s", path.c_str());
		return false;
	}
	cv::Mat bgra;
	if (img.channels() == 4) {
		bgra = img;
	} else if (img.channels() == 3) {
		cv::cvtColor(img, bgra, cv::COLOR_BGR2BGRA);
	} else if (img.channels() == 1) {
		cv::cvtColor(img, bgra, cv::COLOR_GRAY2BGRA);
	} else {
		obs_log(LOG_WARNING, "Face Sticker: unsupported channel count %d", img.channels());
		return false;
	}
	std::lock_guard<std::mutex> lock(f->sticker_mutex);
	f->sticker_bgra = bgra;
	obs_log(LOG_INFO, "Face Sticker: loaded sticker %s (%dx%d)",
		path.c_str(), bgra.cols, bgra.rows);
	return true;
}

// ---------------------------------------------------------------------------
// Helpers: locate the YuNet ONNX model file inside the plugin's data dir.
// ---------------------------------------------------------------------------
std::string find_model_path()
{
	const char *candidates[] = {
		"face_detection_yunet_2023mar.onnx",
		"face_detection_yunet.onnx",
	};
	for (const char *name : candidates) {
		char *p = obs_module_file(name);
		if (p) {
			std::string s = p;
			bfree(p);
			return s;
		}
	}
	return std::string();
}

// ---------------------------------------------------------------------------
// Helpers: ensure detector exists and has correct input size.
// ---------------------------------------------------------------------------
void ensure_detector(FaceStickerFilter *f, int width, int height)
{
	if (f->model_path.empty()) {
		f->model_path = find_model_path();
		if (f->model_path.empty()) {
			obs_log(LOG_ERROR,
				"Face Sticker: could not find YuNet model in plugin data dir");
			return;
		}
	}
	if (!f->detector) {
		try {
			f->detector = cv::FaceDetectorYN::create(
				f->model_path, "", cv::Size(width, height),
				f->score_threshold, 0.3f, 5000);
			f->detector_input_w = width;
			f->detector_input_h = height;
		} catch (const cv::Exception &e) {
			obs_log(LOG_ERROR, "Face Sticker: failed to create detector: %s",
				e.what());
			f->detector.reset();
			return;
		}
	}
	if (f->detector_input_w != width || f->detector_input_h != height) {
		f->detector->setInputSize(cv::Size(width, height));
		f->detector_input_w = width;
		f->detector_input_h = height;
	}
	f->detector->setScoreThreshold(f->score_threshold);
}

// ---------------------------------------------------------------------------
// Convert obs_source_frame to a BGR cv::Mat for face detection
// (and a separate BGRA Mat for in-place compositing destination).
// We always allocate a fresh BGR mat; for compositing we operate directly
// on the frame's planes by going through a per-format path.
// ---------------------------------------------------------------------------
bool frame_to_bgr(const struct obs_source_frame *frame, cv::Mat &bgr)
{
	const int w = static_cast<int>(frame->width);
	const int h = static_cast<int>(frame->height);
	switch (frame->format) {
	case VIDEO_FORMAT_NV12: {
		cv::Mat yuv(h * 3 / 2, w, CV_8UC1);
		// Y plane.
		for (int y = 0; y < h; ++y) {
			std::memcpy(yuv.ptr(y),
				    frame->data[0] + y * frame->linesize[0], w);
		}
		// Interleaved UV plane (h/2 rows, w bytes/row).
		for (int y = 0; y < h / 2; ++y) {
			std::memcpy(yuv.ptr(h + y),
				    frame->data[1] + y * frame->linesize[1], w);
		}
		cv::cvtColor(yuv, bgr, cv::COLOR_YUV2BGR_NV12);
		return true;
	}
	case VIDEO_FORMAT_I420: {
		cv::Mat yuv(h * 3 / 2, w, CV_8UC1);
		for (int y = 0; y < h; ++y)
			std::memcpy(yuv.ptr(y),
				    frame->data[0] + y * frame->linesize[0], w);
		for (int y = 0; y < h / 2; ++y) {
			std::memcpy(yuv.ptr(h + y),
				    frame->data[1] + y * frame->linesize[1], w / 2);
			std::memcpy(yuv.ptr(h + y) + w / 2,
				    frame->data[2] + y * frame->linesize[2], w / 2);
		}
		cv::cvtColor(yuv, bgr, cv::COLOR_YUV2BGR_I420);
		return true;
	}
	case VIDEO_FORMAT_YUY2: {
		cv::Mat yuy2(h, w, CV_8UC2);
		for (int y = 0; y < h; ++y)
			std::memcpy(yuy2.ptr(y),
				    frame->data[0] + y * frame->linesize[0], (size_t)w * 2);
		cv::cvtColor(yuy2, bgr, cv::COLOR_YUV2BGR_YUYV);
		return true;
	}
	case VIDEO_FORMAT_UYVY: {
		cv::Mat uyvy(h, w, CV_8UC2);
		for (int y = 0; y < h; ++y)
			std::memcpy(uyvy.ptr(y),
				    frame->data[0] + y * frame->linesize[0], (size_t)w * 2);
		cv::cvtColor(uyvy, bgr, cv::COLOR_YUV2BGR_UYVY);
		return true;
	}
	case VIDEO_FORMAT_BGRA:
	case VIDEO_FORMAT_BGRX: {
		cv::Mat bgra(h, w, CV_8UC4);
		for (int y = 0; y < h; ++y)
			std::memcpy(bgra.ptr(y),
				    frame->data[0] + y * frame->linesize[0], (size_t)w * 4);
		cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
		return true;
	}
	case VIDEO_FORMAT_RGBA: {
		cv::Mat rgba(h, w, CV_8UC4);
		for (int y = 0; y < h; ++y)
			std::memcpy(rgba.ptr(y),
				    frame->data[0] + y * frame->linesize[0], (size_t)w * 4);
		cv::cvtColor(rgba, bgr, cv::COLOR_RGBA2BGR);
		return true;
	}
	case VIDEO_FORMAT_BGR3: {
		cv::Mat src(h, w, CV_8UC3);
		for (int y = 0; y < h; ++y)
			std::memcpy(src.ptr(y),
				    frame->data[0] + y * frame->linesize[0], (size_t)w * 3);
		bgr = src;
		return true;
	}
	case VIDEO_FORMAT_I444: {
		cv::Mat y_plane(h, w, CV_8UC1);
		cv::Mat u_plane(h, w, CV_8UC1);
		cv::Mat v_plane(h, w, CV_8UC1);
		for (int y = 0; y < h; ++y) {
			std::memcpy(y_plane.ptr(y), frame->data[0] + y * frame->linesize[0], w);
			std::memcpy(u_plane.ptr(y), frame->data[1] + y * frame->linesize[1], w);
			std::memcpy(v_plane.ptr(y), frame->data[2] + y * frame->linesize[2], w);
		}
		cv::Mat yuv;
		std::vector<cv::Mat> ch{y_plane, u_plane, v_plane};
		cv::merge(ch, yuv);
		cv::cvtColor(yuv, bgr, cv::COLOR_YUV2BGR);
		return true;
	}
	case VIDEO_FORMAT_Y800: {
		cv::Mat g(h, w, CV_8UC1);
		for (int y = 0; y < h; ++y)
			std::memcpy(g.ptr(y), frame->data[0] + y * frame->linesize[0], w);
		cv::cvtColor(g, bgr, cv::COLOR_GRAY2BGR);
		return true;
	}
	default:
		return false;
	}
}

// ---------------------------------------------------------------------------
// Write back a BGR Mat into the frame's planes (in-place).
// ---------------------------------------------------------------------------
bool bgr_to_frame(const cv::Mat &bgr, struct obs_source_frame *frame)
{
	const int w = static_cast<int>(frame->width);
	const int h = static_cast<int>(frame->height);
	switch (frame->format) {
	case VIDEO_FORMAT_NV12: {
		cv::Mat yuv_full;
		cv::cvtColor(bgr, yuv_full, cv::COLOR_BGR2YUV);
		std::vector<cv::Mat> ch;
		cv::split(yuv_full, ch);
		cv::Mat u_small, v_small;
		cv::resize(ch[1], u_small, cv::Size(w / 2, h / 2), 0, 0, cv::INTER_LINEAR);
		cv::resize(ch[2], v_small, cv::Size(w / 2, h / 2), 0, 0, cv::INTER_LINEAR);
		for (int y = 0; y < h; ++y)
			std::memcpy(frame->data[0] + y * frame->linesize[0], ch[0].ptr(y), w);
		for (int y = 0; y < h / 2; ++y) {
			uint8_t *dst = frame->data[1] + y * frame->linesize[1];
			const uint8_t *u_row = u_small.ptr(y);
			const uint8_t *v_row = v_small.ptr(y);
			for (int x = 0; x < w / 2; ++x) {
				dst[2 * x] = u_row[x];
				dst[2 * x + 1] = v_row[x];
			}
		}
		return true;
	}
	case VIDEO_FORMAT_I420: {
		cv::Mat yuv_full;
		cv::cvtColor(bgr, yuv_full, cv::COLOR_BGR2YUV);
		std::vector<cv::Mat> ch;
		cv::split(yuv_full, ch);
		cv::Mat u_small, v_small;
		cv::resize(ch[1], u_small, cv::Size(w / 2, h / 2), 0, 0, cv::INTER_LINEAR);
		cv::resize(ch[2], v_small, cv::Size(w / 2, h / 2), 0, 0, cv::INTER_LINEAR);
		for (int y = 0; y < h; ++y)
			std::memcpy(frame->data[0] + y * frame->linesize[0], ch[0].ptr(y), w);
		for (int y = 0; y < h / 2; ++y) {
			std::memcpy(frame->data[1] + y * frame->linesize[1], u_small.ptr(y), w / 2);
			std::memcpy(frame->data[2] + y * frame->linesize[2], v_small.ptr(y), w / 2);
		}
		return true;
	}
	case VIDEO_FORMAT_YUY2: {
		cv::Mat yuy2;
		cv::cvtColor(bgr, yuy2, cv::COLOR_BGR2YUV);
		// Need to re-pack into YUYV. Build manually.
		std::vector<cv::Mat> ch;
		cv::split(yuy2, ch);
		for (int y = 0; y < h; ++y) {
			uint8_t *dst = frame->data[0] + y * frame->linesize[0];
			const uint8_t *yp = ch[0].ptr(y);
			const uint8_t *up = ch[1].ptr(y);
			const uint8_t *vp = ch[2].ptr(y);
			for (int x = 0; x < w; x += 2) {
				dst[2 * x + 0] = yp[x];
				dst[2 * x + 1] = up[x];
				dst[2 * x + 2] = (x + 1 < w) ? yp[x + 1] : yp[x];
				dst[2 * x + 3] = vp[x];
			}
		}
		return true;
	}
	case VIDEO_FORMAT_UYVY: {
		cv::Mat yuv;
		cv::cvtColor(bgr, yuv, cv::COLOR_BGR2YUV);
		std::vector<cv::Mat> ch;
		cv::split(yuv, ch);
		for (int y = 0; y < h; ++y) {
			uint8_t *dst = frame->data[0] + y * frame->linesize[0];
			const uint8_t *yp = ch[0].ptr(y);
			const uint8_t *up = ch[1].ptr(y);
			const uint8_t *vp = ch[2].ptr(y);
			for (int x = 0; x < w; x += 2) {
				dst[2 * x + 0] = up[x];
				dst[2 * x + 1] = yp[x];
				dst[2 * x + 2] = vp[x];
				dst[2 * x + 3] = (x + 1 < w) ? yp[x + 1] : yp[x];
			}
		}
		return true;
	}
	case VIDEO_FORMAT_BGRA:
	case VIDEO_FORMAT_BGRX: {
		cv::Mat bgra;
		cv::cvtColor(bgr, bgra, cv::COLOR_BGR2BGRA);
		for (int y = 0; y < h; ++y)
			std::memcpy(frame->data[0] + y * frame->linesize[0],
				    bgra.ptr(y), (size_t)w * 4);
		return true;
	}
	case VIDEO_FORMAT_RGBA: {
		cv::Mat rgba;
		cv::cvtColor(bgr, rgba, cv::COLOR_BGR2RGBA);
		for (int y = 0; y < h; ++y)
			std::memcpy(frame->data[0] + y * frame->linesize[0],
				    rgba.ptr(y), (size_t)w * 4);
		return true;
	}
	case VIDEO_FORMAT_BGR3: {
		for (int y = 0; y < h; ++y)
			std::memcpy(frame->data[0] + y * frame->linesize[0],
				    bgr.ptr(y), (size_t)w * 3);
		return true;
	}
	case VIDEO_FORMAT_I444: {
		cv::Mat yuv;
		cv::cvtColor(bgr, yuv, cv::COLOR_BGR2YUV);
		std::vector<cv::Mat> ch;
		cv::split(yuv, ch);
		for (int y = 0; y < h; ++y) {
			std::memcpy(frame->data[0] + y * frame->linesize[0], ch[0].ptr(y), w);
			std::memcpy(frame->data[1] + y * frame->linesize[1], ch[1].ptr(y), w);
			std::memcpy(frame->data[2] + y * frame->linesize[2], ch[2].ptr(y), w);
		}
		return true;
	}
	case VIDEO_FORMAT_Y800: {
		cv::Mat g;
		cv::cvtColor(bgr, g, cv::COLOR_BGR2GRAY);
		for (int y = 0; y < h; ++y)
			std::memcpy(frame->data[0] + y * frame->linesize[0], g.ptr(y), w);
		return true;
	}
	default:
		return false;
	}
}

// ---------------------------------------------------------------------------
// Run YuNet detection -> update tracked face list (with simple smoothing).
// ---------------------------------------------------------------------------
void detect_faces(FaceStickerFilter *f, const cv::Mat &bgr)
{
	ensure_detector(f, bgr.cols, bgr.rows);
	if (!f->detector)
		return;

	cv::Mat faces_mat;
	try {
		f->detector->detect(bgr, faces_mat);
	} catch (const cv::Exception &e) {
		obs_log(LOG_WARNING, "Face Sticker: detect() failed: %s", e.what());
		return;
	}

	std::vector<FaceState> raw;
	if (!faces_mat.empty()) {
		const int n = std::min(faces_mat.rows, f->max_faces);
		for (int i = 0; i < n; ++i) {
			const float *row = faces_mat.ptr<float>(i);
			FaceState fs;
			fs.bbox_x = row[0];
			fs.bbox_y = row[1];
			fs.bbox_w = row[2];
			fs.bbox_h = row[3];
			fs.right_eye_x = row[4];
			fs.right_eye_y = row[5];
			fs.left_eye_x = row[6];
			fs.left_eye_y = row[7];
			fs.nose_x = row[8];
			fs.nose_y = row[9];
			fs.right_mouth_x = row[10];
			fs.right_mouth_y = row[11];
			fs.left_mouth_x = row[12];
			fs.left_mouth_y = row[13];
			fs.valid = true;
			raw.push_back(fs);
		}
	}

	// Match new detections to old ones by nearest center (greedy).
	std::vector<FaceState> next(raw.size());
	std::vector<bool> old_used(f->faces.size(), false);
	for (size_t i = 0; i < raw.size(); ++i) {
		float cx = raw[i].bbox_x + raw[i].bbox_w * 0.5f;
		float cy = raw[i].bbox_y + raw[i].bbox_h * 0.5f;
		int best = -1;
		float best_d = std::numeric_limits<float>::max();
		for (size_t j = 0; j < f->faces.size(); ++j) {
			if (old_used[j])
				continue;
			float ocx = f->faces[j].bbox_x + f->faces[j].bbox_w * 0.5f;
			float ocy = f->faces[j].bbox_y + f->faces[j].bbox_h * 0.5f;
			float dx = cx - ocx;
			float dy = cy - ocy;
			float d = dx * dx + dy * dy;
			if (d < best_d) {
				best_d = d;
				best = static_cast<int>(j);
			}
		}
		if (best >= 0 && best_d < (raw[i].bbox_w * raw[i].bbox_w)) {
			old_used[best] = true;
			const FaceState &old = f->faces[best];
			const float a = std::clamp(f->smoothing, 0.0f, 0.95f);
			FaceState s = raw[i];
#define LERP(field) s.field = a * old.field + (1.0f - a) * raw[i].field
			LERP(bbox_x); LERP(bbox_y); LERP(bbox_w); LERP(bbox_h);
			LERP(right_eye_x); LERP(right_eye_y);
			LERP(left_eye_x); LERP(left_eye_y);
			LERP(nose_x); LERP(nose_y);
			LERP(right_mouth_x); LERP(right_mouth_y);
			LERP(left_mouth_x); LERP(left_mouth_y);
#undef LERP
			next[i] = s;
		} else {
			next[i] = raw[i];
		}
	}
	f->faces = std::move(next);
}

// ---------------------------------------------------------------------------
// Compute the placement (center, angle in radians, target width in px) of
// the sticker for a given face according to attach mode and user settings.
// ---------------------------------------------------------------------------
struct Placement {
	cv::Point2f center;
	float angle = 0.f; // radians
	float width = 0.f; // in px
	float aspect = 1.f; // sticker height / width
	bool valid = false;
};

Placement compute_placement(const FaceStickerFilter *f, const FaceState &fs,
			    const cv::Mat &sticker)
{
	Placement p;
	if (!fs.valid || sticker.empty())
		return p;

	const float ex = (fs.right_eye_x + fs.left_eye_x) * 0.5f;
	const float ey = (fs.right_eye_y + fs.left_eye_y) * 0.5f;
	const float dx = fs.left_eye_x - fs.right_eye_x;
	const float dy = fs.left_eye_y - fs.right_eye_y;
	const float angle = f->follow_rotation ? std::atan2(dy, dx) : 0.f;
	const float face_w = std::max(fs.bbox_w, 1.f);
	const float aspect = static_cast<float>(sticker.rows) / static_cast<float>(sticker.cols);

	switch (f->attach_mode) {
	case AttachMode::Eyes: {
		// Width spans about face width * scale. Anchor at eye midpoint.
		p.width = face_w * f->scale;
		p.center = cv::Point2f(ex, ey + f->offset_y * face_w);
		break;
	}
	case AttachMode::FullFace: {
		p.width = face_w * f->scale;
		const float cx = fs.bbox_x + fs.bbox_w * 0.5f;
		const float cy = fs.bbox_y + fs.bbox_h * 0.5f;
		p.center = cv::Point2f(cx, cy + f->offset_y * face_w);
		break;
	}
	case AttachMode::Mouth: {
		p.width = face_w * 0.6f * f->scale;
		const float mx = (fs.right_mouth_x + fs.left_mouth_x) * 0.5f;
		const float my = (fs.right_mouth_y + fs.left_mouth_y) * 0.5f;
		p.center = cv::Point2f(mx, my + f->offset_y * face_w);
		break;
	}
	case AttachMode::Forehead: {
		p.width = face_w * f->scale;
		const float fx = ex;
		const float fy = ey - face_w * 0.4f;
		p.center = cv::Point2f(fx, fy + f->offset_y * face_w);
		break;
	}
	}
	p.angle = angle;
	p.aspect = aspect;
	p.valid = p.width > 1.f;
	return p;
}

// ---------------------------------------------------------------------------
// Composite the sticker (BGRA) onto the BGR frame at the given placement.
// We rotate and scale via warpAffine, producing a BGRA "stamp" the same size
// as the bounding box, then alpha-blend.
// ---------------------------------------------------------------------------
void overlay_sticker(cv::Mat &bgr, const cv::Mat &sticker_bgra,
		     const Placement &p)
{
	if (!p.valid || sticker_bgra.empty())
		return;

	const float target_w = p.width;
	const float target_h = p.width * p.aspect;
	if (target_w < 2.f || target_h < 2.f)
		return;

	// We render into a temporary canvas the size of the rotated bounding box,
	// then blit it onto the frame.
	const float diag = std::sqrt(target_w * target_w + target_h * target_h);
	const int canvas_size = static_cast<int>(std::ceil(diag)) + 2;
	if (canvas_size <= 0 || canvas_size > std::max(bgr.cols, bgr.rows) * 4)
		return;

	cv::Mat canvas(canvas_size, canvas_size, CV_8UC4, cv::Scalar(0, 0, 0, 0));

	// Compose affine: scale sticker -> (target_w, target_h),
	// place at canvas center, rotate by p.angle.
	const cv::Point2f cc(canvas_size * 0.5f, canvas_size * 0.5f);
	const float sx = target_w / static_cast<float>(sticker_bgra.cols);
	const float sy = target_h / static_cast<float>(sticker_bgra.rows);
	const float ca = std::cos(p.angle);
	const float sa = std::sin(p.angle);
	// 2x3 matrix mapping source (sticker) -> canvas.
	cv::Mat M = (cv::Mat_<double>(2, 3) <<
		     sx * ca, -sy * sa,
		     cc.x - 0.5 * (sticker_bgra.cols * sx * ca - sticker_bgra.rows * sy * sa),
		     sx * sa,  sy * ca,
		     cc.y - 0.5 * (sticker_bgra.cols * sx * sa + sticker_bgra.rows * sy * ca));
	cv::warpAffine(sticker_bgra, canvas, M, canvas.size(),
		       cv::INTER_LINEAR, cv::BORDER_TRANSPARENT);

	// Compute top-left placement on the frame so canvas center sits at p.center.
	int x0 = static_cast<int>(std::round(p.center.x - canvas_size * 0.5f));
	int y0 = static_cast<int>(std::round(p.center.y - canvas_size * 0.5f));

	// Clip to frame.
	int sx0 = 0, sy0 = 0;
	int x1 = x0 + canvas_size, y1 = y0 + canvas_size;
	if (x0 < 0) { sx0 = -x0; x0 = 0; }
	if (y0 < 0) { sy0 = -y0; y0 = 0; }
	if (x1 > bgr.cols) x1 = bgr.cols;
	if (y1 > bgr.rows) y1 = bgr.rows;
	const int w = x1 - x0;
	const int h = y1 - y0;
	if (w <= 0 || h <= 0)
		return;

	// Alpha blend: out = src.rgb * a + dst * (1 - a).
	for (int y = 0; y < h; ++y) {
		const uint8_t *src = canvas.ptr<uint8_t>(sy0 + y) + sx0 * 4;
		uint8_t *dst = bgr.ptr<uint8_t>(y0 + y) + x0 * 3;
		for (int x = 0; x < w; ++x) {
			const uint8_t a = src[3];
			if (a == 0) {
				src += 4;
				dst += 3;
				continue;
			}
			if (a == 255) {
				dst[0] = src[0];
				dst[1] = src[1];
				dst[2] = src[2];
				src += 4;
				dst += 3;
				continue;
			}
			const int ia = 255 - a;
			dst[0] = static_cast<uint8_t>((src[0] * a + dst[0] * ia + 127) / 255);
			dst[1] = static_cast<uint8_t>((src[1] * a + dst[1] * ia + 127) / 255);
			dst[2] = static_cast<uint8_t>((src[2] * a + dst[2] * ia + 127) / 255);
			src += 4;
			dst += 3;
		}
	}
}

// ---------------------------------------------------------------------------
// OBS source callbacks.
// ---------------------------------------------------------------------------
const char *fs_get_name(void *)
{
	return obs_module_text("FaceSticker.Name");
}

void fs_update(void *data, obs_data_t *settings)
{
	auto *f = static_cast<FaceStickerFilter *>(data);

	const char *path = obs_data_get_string(settings, kSettingImagePath);
	std::string new_path = path ? path : "";
	if (new_path != f->image_path) {
		f->image_path = new_path;
		load_sticker(f, f->image_path);
	}
	f->attach_mode = static_cast<AttachMode>(
		obs_data_get_int(settings, kSettingAttach));
	f->scale = static_cast<float>(obs_data_get_double(settings, kSettingScale));
	f->offset_y = static_cast<float>(obs_data_get_double(settings, kSettingOffsetY));
	f->follow_rotation = obs_data_get_bool(settings, kSettingFollowRotation);
	f->detect_interval = std::max(1, static_cast<int>(obs_data_get_int(settings, kSettingDetectInterval)));
	f->smoothing = static_cast<float>(obs_data_get_double(settings, kSettingSmoothing));
	f->max_faces = std::max(1, static_cast<int>(obs_data_get_int(settings, kSettingMaxFaces)));
	f->score_threshold = static_cast<float>(obs_data_get_double(settings, kSettingScoreThreshold));
}

void *fs_create(obs_data_t *settings, obs_source_t *source)
{
	auto *f = new FaceStickerFilter();
	f->self = source;
	fs_update(f, settings);
	return f;
}

void fs_destroy(void *data)
{
	auto *f = static_cast<FaceStickerFilter *>(data);
	delete f;
}

void fs_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, kSettingImagePath, "");
	obs_data_set_default_int(settings, kSettingAttach, static_cast<int>(AttachMode::Eyes));
	obs_data_set_default_double(settings, kSettingScale, 1.0);
	obs_data_set_default_double(settings, kSettingOffsetY, 0.0);
	obs_data_set_default_bool(settings, kSettingFollowRotation, true);
	obs_data_set_default_int(settings, kSettingDetectInterval, 2);
	obs_data_set_default_double(settings, kSettingSmoothing, 0.6);
	obs_data_set_default_int(settings, kSettingMaxFaces, 4);
	obs_data_set_default_double(settings, kSettingScoreThreshold, 0.7);
}

obs_properties_t *fs_properties(void *)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_path(props, kSettingImagePath,
				obs_module_text("FaceSticker.Image"),
				OBS_PATH_FILE,
				"Image (*.png *.jpg *.jpeg *.bmp *.webp);;All (*.*)",
				nullptr);

	obs_property_t *attach = obs_properties_add_list(
		props, kSettingAttach, obs_module_text("FaceSticker.Attach"),
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(attach, obs_module_text("FaceSticker.Attach.Eyes"),
				  static_cast<int>(AttachMode::Eyes));
	obs_property_list_add_int(attach, obs_module_text("FaceSticker.Attach.FullFace"),
				  static_cast<int>(AttachMode::FullFace));
	obs_property_list_add_int(attach, obs_module_text("FaceSticker.Attach.Mouth"),
				  static_cast<int>(AttachMode::Mouth));
	obs_property_list_add_int(attach, obs_module_text("FaceSticker.Attach.Forehead"),
				  static_cast<int>(AttachMode::Forehead));

	obs_properties_add_float_slider(props, kSettingScale,
					obs_module_text("FaceSticker.Scale"),
					0.2, 3.0, 0.01);
	obs_properties_add_float_slider(props, kSettingOffsetY,
					obs_module_text("FaceSticker.OffsetY"),
					-1.0, 1.0, 0.01);
	obs_properties_add_bool(props, kSettingFollowRotation,
				obs_module_text("FaceSticker.FollowRotation"));
	obs_properties_add_int_slider(props, kSettingDetectInterval,
				      obs_module_text("FaceSticker.DetectInterval"),
				      1, 10, 1);
	obs_properties_add_float_slider(props, kSettingSmoothing,
					obs_module_text("FaceSticker.Smoothing"),
					0.0, 0.95, 0.01);
	obs_properties_add_int_slider(props, kSettingMaxFaces,
				      obs_module_text("FaceSticker.MaxFaces"),
				      1, 8, 1);
	obs_properties_add_float_slider(props, kSettingScoreThreshold,
					obs_module_text("FaceSticker.ScoreThreshold"),
					0.3, 0.99, 0.01);
	return props;
}

struct obs_source_frame *fs_filter_video(void *data, struct obs_source_frame *frame)
{
	auto *f = static_cast<FaceStickerFilter *>(data);
	if (!frame || !f->active.load())
		return frame;

	cv::Mat sticker;
	{
		std::lock_guard<std::mutex> lock(f->sticker_mutex);
		sticker = f->sticker_bgra; // shallow copy, refcounted
	}
	if (sticker.empty())
		return frame; // no overlay

	cv::Mat bgr;
	if (!frame_to_bgr(frame, bgr))
		return frame; // unsupported format -> pass-through

	// Detection (every N frames). On other frames we reuse last known faces.
	if ((f->frame_counter % f->detect_interval) == 0) {
		detect_faces(f, bgr);
	}
	++f->frame_counter;

	for (const auto &fs : f->faces) {
		Placement pl = compute_placement(f, fs, sticker);
		overlay_sticker(bgr, sticker, pl);
	}

	bgr_to_frame(bgr, frame);
	return frame;
}

void fs_show(void *data) { static_cast<FaceStickerFilter *>(data)->active = true; }
void fs_hide(void *data) { static_cast<FaceStickerFilter *>(data)->active = false; }

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public registration.
// ---------------------------------------------------------------------------
extern "C" void face_sticker_register(void)
{
	struct obs_source_info info = {};
	info.id = "manus_face_sticker";
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_ASYNC_VIDEO;
	info.get_name = fs_get_name;
	info.create = fs_create;
	info.destroy = fs_destroy;
	info.update = fs_update;
	info.get_defaults = fs_defaults;
	info.get_properties = fs_properties;
	info.filter_video = fs_filter_video;
	info.show = fs_show;
	info.hide = fs_hide;
	obs_register_source(&info);
}
