# Face Sticker for OBS Studio

**Face Sticker** は、OBS Studio の映像ソース（Web カメラ、キャプチャデバイス、メディアソースなど）に対し、検出した人物の顔へ任意の画像（透過 PNG など）を**リアルタイムで追従させる**ネイティブフィルタです。サングラス・帽子・ヒゲ・マスクといった用途に使えます。

> 仮想カメラを介さず OBS のソース上に直接フィルタとして適用するため、画質劣化や追加遅延を最小限に抑えられます。

---

## 主な特徴

- **画像をいつでも差し替え可能**：プラグインのプロパティ画面でファイルパスを指定するだけ。透過 PNG／JPEG／WebP 等。
- **顔の位置・大きさ・傾きに追従**：両目の位置から自動で角度と幅を算出。
- **複数人対応**：同時に最大 8 人まで（設定可能）。
- **貼り付け位置を選択可能**：
  - `目元（サングラス）`
  - `顔全体（マスク）`
  - `口元（ヒゲ）`
  - `額（帽子）`
- **滑らかさ調整**：スムージング係数で揺れを抑制。
- **負荷を抑える間引き検出**：Nフレームに 1 回だけ顔検出を実行（既定 2）。

顔検出には [OpenCV YuNet](https://github.com/opencv/opencv_zoo/tree/main/models/face_detection_yunet)（軽量 ONNX モデル、約 230 KB）を利用しています。GPU は不要で、720p 30fps 程度なら CPU 上で十分リアルタイム動作します。

---

## 動作要件

- Windows 10/11 (64bit)
- OBS Studio 31.x 以上

---

## インストール

1. [Releases](../../releases) から `face-sticker-<version>-windows-x64.zip` をダウンロード。
2. ZIP を展開すると `obs-plugins\64bit\face-sticker.dll` と `data\obs-plugins\face-sticker\...` が含まれます。
3. それらを **`<OBS インストール先>\obs-plugins\64bit\`** および **`<OBS インストール先>\data\obs-plugins\face-sticker\`** に配置（既定では `C:\Program Files\obs-studio\` 以下）。
4. OBS を起動。

---

## 使い方

1. OBS で対象の **映像キャプチャデバイス** や **メディアソース** を選択。
2. 右クリック → 「フィルタ」を開く。
3. 「+」→ **`Face Sticker`** を追加。
4. プロパティ画面で:
   - **Sticker image**: 重ねたい画像（透過 PNG 推奨）を選択。
   - **Attach to**: `目元（サングラス）` 等、貼り付け位置。
   - **Size**: 顔幅に対する倍率（既定 1.0）。
   - **Vertical offset**: 縦方向の微調整。
   - **Follow head rotation**: 顔の傾きに合わせて回転させるか。
   - **Detect every N frames / Smoothing / Max faces / Score threshold**: 性能・精度の調整。

画像はいつでもプロパティから差し替えできます。シーンコレクションに保存されるので OBS を再起動しても残ります。

---

## 推奨：サングラス用の画像作り方の例

- 透過 PNG。横長で、左右の中央が鼻位置、上下中央が目の位置になるよう配置。
- だいたい 600 × 200 px 程度あれば十分。

---

## ビルド方法（開発者向け）

ビルドは GitHub Actions による自動ビルドを推奨します。リポジトリを Push すれば、Windows 用 `.zip` がアーティファクトとして生成されます。

ローカルで手元 Windows ビルドをする場合：

```powershell
# 事前に Visual Studio 2022 + CMake 3.30 以上 + vcpkg をインストール
$Env:VCPKG_INSTALLATION_ROOT = "C:\path\to\vcpkg"
& "$Env:VCPKG_INSTALLATION_ROOT\vcpkg.exe" install "opencv4[core,dnn,jpeg,png]:x64-windows" --recurse
$Env:CMAKE_TOOLCHAIN_FILE = "$Env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake"
$Env:VCPKG_TARGET_TRIPLET = "x64-windows"
$Env:CI = "true"
.\.github\scripts\Build-Windows.ps1 -Target x64 -Configuration RelWithDebInfo
```

成果物は `release/RelWithDebInfo/` 以下に生成されます。

---

## ライセンス

GPL-2.0-or-later。詳細は `LICENSE` を参照してください。

同梱の YuNet ONNX モデル（`data/face_detection_yunet_2023mar.onnx`）は [opencv_zoo](https://github.com/opencv/opencv_zoo) のライセンス（MIT）に従います。
