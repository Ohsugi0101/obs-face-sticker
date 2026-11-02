# 顔追従PNGオーバーレイ — OBS Studio用ネイティブフィルター

透過PNGを1人の顔の検出点に重ねるC++プラグインです。ソースコード、固定バージョンのモデル、CMake設定、日本語UI、Windowsのビルド・インストールスクリプトを含みます。

**Windows x64のMSVCビルド・自動テスト・ZIP生成を確認済みです。** [成功したGitHub Actions](https://github.com/Ohsugi0101/obs-face-sticker/actions/runs/41320563410)。ビルド済みZIPにはDLL・モデル・シェーダー・日本語UI・インストールスクリプトを含みます。WindowsのOBS実機でのカメラ追従・描画・安定性は未検証です。詳細は [docs/VALIDATION.md](docs/VALIDATION.md) を参照してください。

## 対応前提

- Windows 11 x64、OBS Studio **31.1.1以上の64bit版**を対象とします。
- 公式 `obsproject/obs-plugintemplate` をベースに、付属 `buildspec.json` のOBS 31.1.1 SDKと依存ファイルのSHA-256固定を維持しています。31/32/33系それぞれのWindowsロード・描画確認は必要です。将来の互換性は保証できません。
- 開発時に確認できたローカルOBSは **macOS版32.2.2**です。ユーザーのWindows上のOBSバージョンはこの環境から確認できません。Windowsのインストーラーはインストール先のバージョンを表示・下限チェックします。
- **SDR（sRGB）のソースが対象**です。HDRソースでは元映像を通過させ、追跡・合成しません。
- 常時ローカル推論。実行時のネットワーク・Python・外部カメラアプリは不要です。

## 技術選定

| 候補 | Windows C++と組み込み | 検出点・負荷 | 採否・配布 |
|---|---|---|---|
| MediaPipe Face Landmarker | C++ Frameworkは存在。ただしFace Landmarkerの公式導入ガイドはAndroid/Python/Web中心で、Windows用のそのままリンクできるCMake SDKではない | 密な3Dランドマーク・表情係数に有利。実際の負荷は構成と機器次第 | Bazel/実行グラフ等の統合負担を避け、本実装では不採用。ライブラリ本体と個別モデルのライセンス確認が別途必要 |
| OpenCV 4.10.0 + YuNet 2023mar | `cv::FaceDetectorYN` のC++ API。OpenCVをMSVCで静的リンクし、追加DLLの探索競合を回避 | 両目・鼻・両口角の**5点を実際に推論**。320px以下、CPU推論、約15Hzの取得。対象PCでの時間・使用率は未測定 | 採用。OpenCV Apache-2.0、YuNetモデルとディレクトリMIT。モデル約233KBを同梱しSHA-256検証 |

鼻・左右の目は検出点、口は両口角の中心です。**額・左右の頬・顎は5点からの2D幾何推定**で、密な顔メッシュによる当該部位の直接検出ではありません。横顔、遮蔽、強い表情、遠く小さい顔では精度が下がります。眼間距離でサイズ、眼を結ぶ線でロール角を推定します。遠近法によるPNGの変形、yaw/pitchの3D回転、顔によるPNGの遮蔽は行いません。

参考一次資料：

- [OBS公式プラグインテンプレート](https://github.com/obsproject/obs-plugintemplate)
- [OBS Source API](https://docs.obsproject.com/reference-sources)
- [OpenCV 4.10 FaceDetectorYN](https://docs.opencv.org/4.10.0/df/d20/classcv_1_1FaceDetectorYN.html)
- [固定コミットのYuNetモデル・ライセンス](https://github.com/opencv/opencv_zoo/tree/48b10fa505aeb51fbd127fc60a5c660040e823b7/models/face_detection_yunet)
- [MediaPipe Face Landmarker公式ガイド](https://developers.google.com/edge/mediapipe/solutions/vision/face_landmarker)

## 友人・一般利用者向けダウンロード

[ReleasesからWindows用ZIPをダウンロード](https://github.com/Ohsugi0101/obs-face-sticker/releases/tag/v0.1.0-beta.1)してください。「Source code」は開発者用です。
ZIPを「すべて展開」し、OBS終了後に **INSTALL.cmdをダブルクリック** → 管理者確認で「はい」を選ぶとインストールできます。詳しくは [QUICKSTART.txt](QUICKSTART.txt) を参照してください。
Windows OBS実機での動作は未検証のベータ版です。不具合は [Issues](https://github.com/Ohsugi0101/obs-face-sticker/issues) へ、OBSバージョンと再現手順を添えてください。

## WindowsでビルドしてZIPを作る

1. Visual Studio 2022の「C++によるデスクトップ開発」、MSVC v143、Windows SDK **10.0.22621以上**をインストールします。CMake 3.30以上、Git、Python 3をPATHに追加します。CMake 4.xにも互換ポリシー設定を入れています。
2. このプロジェクト一式を展開します。初回は数GBの空き容量、依存ファイル取得のためのインターネット接続が必要です。
3. PowerShellでプロジェクトディレクトリへ移動し、実行します。

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-windows.ps1
```

スクリプトはモデルの検証、公式テンプレートSDK取得、OpenCV 4.10.0のソース取得・静的ビルド、プラグインビルド、単体テスト、パッケージ生成を行います。初回は長時間かかります。メモリが不足する場合は `-Jobs 2` を指定してください。SDK/OpenCVのビルド出力を削除しない限り再ビルド時に利用します。

生成先：`dist/face-png-filter-0.1.0-windows-x64.zip`

個別コマンド：

```powershell
python scripts/setup-model.py
cmake --preset windows-x64 -DBUILD_TESTING=ON
cmake --build build_x64 --config RelWithDebInfo --parallel 4
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure
cmake --install build_x64 --config RelWithDebInfo --prefix dist/stage
```

ZIP化にはビルドスクリプトを使用してください。単独の `cmake --install` はプラグインに加えてOpenCVの開発用SDKも配置します。ビルドスクリプトは必要なファイルだけをZIPに含めます。
GitHub Actionsの `.github/workflows/build-windows.yml` でも同じビルドが可能です。本納品では既存の非公開リポジトリ `Ohsugi0101/obs-face-sticker` の専用ブランチから、同じビルドスクリプトを実行して成功しています。

## OBSへインストール

1. OBSを終了します。
2. **Windowsで生成したバイナリZIP**を展開します（ソースZIPだけではインストールできません）。
3. 標準のProgram Filesインストールでは、管理者PowerShellで展開先のスクリプトを実行します。

```powershell
powershell -ExecutionPolicy Bypass -File .\install-windows.ps1
# ポータブルOBSの場合：
powershell -ExecutionPolicy Bypass -File .\install-windows.ps1 -OBSRoot D:\OBS
```

手動でコピーする場合：

```text
ZIP/face-png-filter/bin/64bit/face-png-filter.dll
  → <OBS>/obs-plugins/64bit/face-png-filter.dll
ZIP/face-png-filter/data/*
  → <OBS>/data/obs-plugins/face-png-filter/*
```

必要なMSVCランタイムがない場合はMicrosoftのVisual C++ 2015–2022 x64再頒布可能パッケージを入れてください。ランタイムは同梱していません。プラグイン専用の追加OpenCV DLLは不要です。アンインストールはOBS終了後に上記DLLと専用データディレクトリを削除します。

## 使い方

1. OBSにWebカメラを「映像キャプチャデバイス」として追加。
2. 対象ソースの「フィルタ」→「エフェクトフィルタ」の＋から「顔追従PNGオーバーレイ」を追加。
3. 「透過PNGファイル」で任意のPNGを選び、「追従位置」を「鼻」に設定。
4. 必要に応じて拡大率、位置補正、平滑化を調整。
5. デバッグの5点と追従マーカーで確認後、配信前にOFFにします。デバッグ描画も録画・配信に入ります。

PNGなし・読込失敗・顔なしでは元映像を表示します。PNGの変更は即時反映します。同じファイルの内容を書き換えたときは「PNGを再読み込み」を押してください。ファイル削除後は、既に読み込んだGPU画像を再読込まで保持します。画像のデコードとGPUアップロードはファイル変更・再読込時のみです。

| 設定 | 意味 |
|---|---|
| 拡大率 | 1で画像の幅が両目間距離と同じ。自動拡縮OFFでは100px幅を基準 |
| 不透明度 | 0〜100%。PNGのアルファ値にも乗算 |
| 基準点X/Y | 0〜1。通常0.5/0.5で画像中央。0/0なら左上 |
| 左目・右目 | 画面の左右ではなく、映っている本人の左右 |
| カスタム位置 | 鼻原点でX/Y補正。全ての追従位置で同じ補正を利用可能 |
| X/Y補正 | 両目間距離を1とした顔のローカル座標。X正で画面右、Y正で下。顔のロールに合わせて回る |
| 回転補正 | 度。自動回転ON/OFFいずれでも適用 |
| 平滑化 | 0は即時、1は強く平滑化。座標・眼間距離・最短経路の角度に時間依存EMA |
| 保持時間 | 検出失敗後の表示保持。標準0.35秒。推論結果が途絶えた場合は約0.3秒で失敗扱い |
| 非表示設定OFF | 初回検出後は最後の顔位置に表示を保持。初回未検出時は表示しない |
| 左右反転補正 | 追従座標のみ反転。元映像やPNGは反転しない。通常OFF |

設定はOBSのソース設定（シーンコレクション）に保存されます。PNGパスは外部ファイルへの絶対パスなので、別PCへ移す場合は再指定してください。

## フィルターチェーン・反転・クロップ

フィルターは `obs_filter_get_target` の映像を一度テクスチャに描画し、その**同じ入力画像**を推論用に縮小します。前段フィルターでクロップ・反転した結果を入力として追跡し、後段のフィルターには合成済み映像を渡します。シーンアイテム側の変換・反転は映像とPNG両方に適用されるので、通常は左右反転補正をOFFにしてください。二重補正はずれの原因になります。

寸法が変わった場合はGPU読み戻し面を再作成し、世代番号で旧寸法の検出結果を破棄します。同寸法のままカメラやクロップ内容が変わった場合は、一時的に古い位置が残る場合があります。

## 実装と負荷

- `src/plugin.cpp`：OBSフィルター登録、ソースのGPUキャプチャ、PNG・デバッグ合成、日本語プロパティ。
- `src/detector.cpp`：YuNet CPU推論。最新1フレームのメールボックス方式でキューを蓄積しない。
- `src/tracking.hpp`：追従点、2D幾何、時間依存EMA、1人の選択維持。
- `data/overlay.effect`：straight-alpha PNGをpremultiplied alphaへ変換して合成。
- 長辺320px以下（各辺32の倍数）、約15Hz、推論内部1スレッドでGPU→CPUへ読み戻し。2枚のステージング面を交互使用し、前回の転送だけをmap。**完全な非同期転送保証はなく、ドライバー次第でmapが待つ可能性があります。**
- OBS描画スレッドでは推論しません。GPUリソースはグラフィックスコンテキストで作成・更新・解放します。設定と結果は短いmutex、フレーム受渡しはtry-lockとバッファswap。ワーカー終了をjoinしてからGPUリソースを破棄します。
- 最初の数フレームや寸法変更時を除きCPUフレーム用バッファを再利用します。画像の初回デコード・変更時は描画スレッドで単発の処理があるため、大きいPNGでは一時停止の可能性があります。小さめのPNGを推奨します。
- 元映像の時間を遅延させず、検出結果を平滑化して最新映像に重ねます。追跡には読み戻し1サンプル分（約67ms）＋推論時間＋平滑化の遅れがあります。未来予測や密なフレーム補間はありません。
- 720p30を目標にしていますが実機負荷は未測定です。60fpsは描画側で利用できますが追跡更新は15Hzです。
- 別人への切り替えは前回矩形との距離・サイズで抑制し、約1.2秒見つからない場合は最大面積の顔へ再選択します。顔認証・本人識別ではなく、交差する人物の完全な識別はできません。

## ライセンスとモデル

本プラグインおよび公式テンプレート部分はGPL-2.0-or-laterです。OpenCVと組み合わせてバイナリを配布する際は、Apache-2.0との互換性のため **GPL-3.0-or-laterの条件で配布**してください。対応するソースとライセンス通知の提供が必要です。詳細は [docs/THIRD_PARTY.md](docs/THIRD_PARTY.md) および `data/licenses/` を参照してください。

モデルは標準で同梱されています。欠損時は `python scripts/setup-model.py` で固定コミットから再取得しSHA-256を検証します。初期化・推論エラー時にはOBSログへ出力し、元映像を通過させます。モデルを修復した後はフィルターを一度削除して再追加してください。学習やクラウド送信は行いません。

## テストと制限

[実機テスト手順](docs/TESTING.md) と [実施済み検証記録](docs/VALIDATION.md) を確認してください。Windows MSVCビルド・追跡数学テスト・x64 DLLの必須exportとモデルを含むZIP構成の検査は完了しています。主要な未検証事項はWindows D3D11上の描画・色・アルファ、実カメラでの追従、720p30/60fpsの負荷、OBS終了時の実機動作です。これらを確認して初めて、依頼の完成条件1〜9を実機で判定できます。
