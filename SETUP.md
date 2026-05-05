# Face Sticker のセットアップ手順（Windows）

このプラグインを実機（Windows + OBS Studio）で動かすまでの手順です。**ビルドは GitHub Actions に任せる**前提なので、Windows 上に Visual Studio や CMake をインストールする必要はありません。

---

## 全体の流れ

1. 本ソース一式（`face-sticker/` フォルダ）を GitHub リポジトリに push する
2. GitHub Actions が自動で Windows 用の `.dll` と必要 DLL をビルド
3. ビルド済みアーティファクトをダウンロード
4. OBS Studio のプラグインフォルダに配置
5. OBS でフィルタとして使う

所要時間は GitHub Actions のビルド込みで約 **15–25 分**（OpenCV を vcpkg で初回ビルドするため）。2 回目以降はキャッシュが効いて数分で済みます。

---

## 1. GitHub リポジトリを作る

GitHub で空のリポジトリ（例: `obs-face-sticker`、Public でも Private でも可）を作成し、ローカルに本プロジェクトを Push します。

```bash
cd path/to/face-sticker
git init
git add .
git commit -m "Initial commit: Face Sticker plugin for OBS"
git branch -M main
git remote add origin git@github.com:YOUR_NAME/obs-face-sticker.git
git push -u origin main
```

push が完了すると、`Build Project` という GitHub Actions ワークフローが自動で走ります。

> **補足**：`buildspec.json` の `name` が `face-sticker`、リポジトリ名は何でも構いません。ワークフロー名やアーティファクト名はリポジトリ名ではなく `buildspec.json` の `name` を使います。

---

## 2. ビルドの完了を待つ

リポジトリの **Actions** タブを開き、`Build Project` の最新ジョブが緑（成功）になるまで待ちます。

- 失敗した場合、ログに OpenCV のビルドエラーや CMake の警告が出ていることがあります。多くの場合、Windows ランナーの vcpkg 状態に依存するため、もう一度 push しなおす（あるいは workflow を re-run する）と通ることが多いです。
- ビルドの成功後、ジョブ画面を一番下までスクロールすると **Artifacts** という項目が表示されます。

---

## 3. アーティファクトをダウンロード

`face-sticker-0.1.0-windows-x64-XXXXXXXXX` という名前の zip がアーティファクトに作成されます。これを Windows PC にダウンロードして展開してください。

zip の中身は次のような構造になっています:

```
face-sticker-0.1.0-windows-x64/
├─ obs-plugins/
│   └─ 64bit/
│       ├─ face-sticker.dll
│       ├─ face-sticker.pdb            (デバッグシンボル / 任意)
│       ├─ opencv_core4.dll
│       ├─ opencv_imgproc4.dll
│       ├─ opencv_imgcodecs4.dll
│       ├─ opencv_dnn4.dll
│       ├─ opencv_objdetect4.dll
│       ├─ libpng16.dll, jpeg62.dll, ... (vcpkg 由来のサポート DLL)
│       └─ ...
└─ data/
    └─ obs-plugins/
        └─ face-sticker/
            ├─ face_detection_yunet_2023mar.onnx
            └─ locale/
                ├─ en-US.ini
                └─ ja-JP.ini
```

---

## 4. OBS Studio に配置

OBS Studio の既定インストール先は `C:\Program Files\obs-studio\` です。

1. **OBS をいったん終了**します。
2. zip 内の `obs-plugins\64bit\*` をすべて、`C:\Program Files\obs-studio\obs-plugins\64bit\` にコピー（管理者権限が必要）。
3. zip 内の `data\obs-plugins\face-sticker\` フォルダを `C:\Program Files\obs-studio\data\obs-plugins\face-sticker\` にコピー。
4. OBS を再起動します。

> **ユーザーフォルダにインストールしたい場合**: `%AppData%\obs-studio\plugins\face-sticker\bin\64bit\face-sticker.dll` と `%AppData%\obs-studio\plugins\face-sticker\data\` に配置することもできます（OBS 28.x 以降で対応）。

---

## 5. フィルタとして使う

1. OBS で Web カメラ（映像キャプチャデバイス）等のソースを右クリック → **「フィルタ」** を選択。
2. 左下の `+` → **「Face Sticker」**（日本語環境では「顔ステッカー」）を選んで追加。
3. プロパティ画面で:
   - **Sticker image**: 重ねたい透過 PNG ファイルを選択。
   - **Attach to**: 「目元（サングラス）」など貼り付け位置を選ぶ。
   - **Size / Vertical offset**: 大きさと縦位置の微調整。
   - **Follow head rotation**: 頭の傾きに合わせて回転。
   - **Detect every N frames / Smoothing / Max faces / Score threshold**: 性能と精度のバランス調整。

画像はいつでも変更できます。シーンコレクションに保存されるので OBS を閉じても設定は残ります。

---

## トラブルシュート

| 症状 | 確認するポイント |
|---|---|
| フィルタ一覧に `Face Sticker` が出ない | DLL の配置先が違う／OpenCV 関連 DLL が同じ `64bit` フォルダに入っていない |
| OBS のログに `failed to find YuNet model` | `data\obs-plugins\face-sticker\face_detection_yunet_2023mar.onnx` が無い |
| 顔検出されない | `Score threshold` を 0.5 程度に下げる、Web カメラの解像度を 720p 以上にする |
| カクつく／重い | `Detect every N frames` を 3〜5 に上げる、`Smoothing` を 0.7〜0.9 に |
| 画像の位置がズレる | `Vertical offset` で微調整、または画像の余白配置を見直す（横方向中央＝鼻位置、上下中央＝目の位置になるように作る） |

---

## ローカル（Windows）でビルドする場合

Visual Studio 2022 + CMake 3.30 以降 + vcpkg を準備した上で、PowerShell から:

```powershell
$Env:VCPKG_INSTALLATION_ROOT = "C:\path\to\vcpkg"
& "$Env:VCPKG_INSTALLATION_ROOT\vcpkg.exe" install "opencv4[core,dnn,jpeg,png]:x64-windows" --recurse
$Env:CMAKE_TOOLCHAIN_FILE = "$Env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake"
$Env:VCPKG_TARGET_TRIPLET = "x64-windows"
$Env:CI = "true"
.\.github\scripts\Build-Windows.ps1 -Target x64 -Configuration RelWithDebInfo
```

成果物は `release\RelWithDebInfo\face-sticker\` 以下に生成されます。
