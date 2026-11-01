# 検証記録

実施日：2026-11-01（日本時間）

## 実際に確認した環境

- macOS arm64、Apple M4、AppleClang 21
- インストール済みOBS Studio 32.2.2の `libobs.framework` と `libobs-opengl.dylib`
- OBSソースタグ32.0.4のヘッダー、SIMDe v0.8.2
- 別途、対象SDKと同じOBS 31.1.1タグのヘッダーでも本体2翻訳単位を構文チェック
- CMake 4.4.4、Ninja 1.13.2
- OpenCV 4.10.0を固定SHA-256のソースから静的ビルド
- YuNet 2023marの同梱モデル

## 実施結果

| 検証 | 結果 |
|---|---|
| 依存なし追従数学テストのCMake設定・コンパイル・CTest | 成功（1/1） |
| OpenCVを含むCMake設定・実コードのコンパイル | 成功 |
| `face-png-filter.so` を実際のmacOS libobsへリンク | 成功。Windows DLLではないため納品ZIPに含めない |
| OBS 31.1.1ヘッダーでplugin.cpp/detector.cppをコンパイラー構文チェック | 成功 |
| OpenCV込みのCTest | 成功（2/2：tracking、detector） |
| 実人物の静止画像で5点ランドマーク推論 | 成功。OpenCV公式サンプル画像を検証用に取得し320×320 RGBAへ変換。画像は配布しない |
| 黒フレームの未検出、最新フレーム受渡し、キュー投入中の破棄 | 成功 |
| モデル欠損時のエラー状態と終了 | 成功 |
| モデル・ライセンスのSHA-256検証スクリプト | 成功 |
| 実libobsの独立プロセスでプラグインロード・フィルター登録 | 成功 |
| OpenGLでoverlay.effectのコンパイル、顔画像へのPNG合成 | 成功。半透明赤いPNGにより673画素が変化 |
| PNG欠損時の通過映像 | 元画像と比較して、1階調超の相違バイト数0 |
| 上記グラフィックステスト後のフィルター削除・ソース解放・obs_shutdown | 正常終了 |

グラフィックス検証は起動中のユーザーのOBSを操作せず、`tests/obs-smoke.cpp` の独立プロセスで行いました。320×320のテストソースを30fpsでlibobsに出力し、モデルの実推論とPNG合成を同時に動作させています。実カメラでの追跡やWindowsでの結果を示すものではありません。

## 未検証

- **Windows 11 / MSVCでのビルド・DLLロード・インストールスクリプト実行**
- Windows D3D11上のステージング転送、シェーダー、PNGのアルファ・色管理
- 実カメラの移動、傾き、近接・遠方、再接続
- Windows版OBS31/32/33の個別互換性
- 1280×720/30fps、60fpsの負荷・長時間配信・録画
- 複数フィルターチェーン、実機でのクロップ・左右反転
- 実OBS UIでの設定保存復元、終了・繰り返し削除、GPUメモリ増減
- GitHub Actionsのリモート実行、Windows用バイナリZIP生成

したがって、現段階は**実装とmacOS libobsでの合成試験を終えたソース納品**です。Windowsで完成条件を満たした「検証済み完成版」とは扱っていません。

## 開発中に修正したビルド問題

- OpenCVをサブプロジェクトとして組み込んだ際、未使用のOrbbecセンサー検出がトップ階層のパスを参照して構成に失敗。`WITH_OBSENSOR=OFF`で無効化。
- OpenCV生成ヘッダーがトップのバイナリディレクトリに置かれるため、そのディレクトリをinclude pathへ追加。
- ローカルarm64検証のOBSヘッダーにはSIMDeおよび生成obsconfig.hが必要だったため、検証用環境に用意。Windowsの公式SDK構成とは別。
- Windowsのランタイム不一致を避けるため、静的OpenCVにも `BUILD_WITH_STATIC_CRT=OFF`（/MD）を明示。Windowsでの実ビルド確認は未実施。

## グラフィックステストの再現（開発者向け）

`FPF_OBS_SMOKE=ON`でビルドした `obs-smoke` に以下の6引数を渡します。

```text
obs-smoke <プラグインバイナリ> <dataディレクトリ> <320x320 raw RGBA顔画像>
          <透過PNG> <libobs graphicsモジュール> <libobs effectデータディレクトリ>
```

テストは現在の作業ディレクトリへ `obs-smoke-output.rgba` を出力します。テスト用画像と出力画像は配布しません。WindowsではOBSのbin/64bitをDLL探索パスへ入れ、graphicsモジュールにlibobs-d3d11を指定したうえで別途検証してください。
