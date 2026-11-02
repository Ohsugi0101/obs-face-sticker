# 検証記録

実施日：macOS検証 2026-11-01、Windowsビルド検証 2026-11-02（日本時間）

## Windows x64ビルド結果（2026-11-02追記）

- 実行環境：GitHub Actions `windows-2022`（Windows Server 2022 x64）
- MSVC 19.44.35229.0、Visual Studio 2022、Windows SDK 10.0.22621.0、CMake 3.31.6
- OBS SDK 31.1.1、OpenCV 4.10.0静的リンク、Release構成
- [成功した実行](https://github.com/Ohsugi0101/obs-face-sticker/actions/runs/41320563410)、ソースコミット `ad75e9ebf`（専用ブランチ `release/face-png-filter-build-20261101`）
- DLLのコンパイル・最適化リンク：成功
- CTest：**1/1成功**（tracking）。Windowsでの実モデル推論テストは未実施
- x64 PE32+形式、OBS必須export、ZIP内リソース、モデルSHA-256の検査：成功
- WindowsバイナリZIP生成・GitHub Artifactへのアップロード：成功
- DLL SHA-256：`f6857ef930d5d661299fbe7afc8838a717f0de7a3c615c264e81d3dbd739a1b0`
- 主な実行時依存：`obs.dll`、Microsoft VC++ Runtime/UCRT（OpenCV DLLは不要）

CIの元ZIPにはOpenCVの開発用SDKも含まれたため、納品ZIPは必要なプラグイン・データ・説明書に絞って再圧縮しました。**DLL・PDB・モデル・シェーダーはCI成果物と同一**です。説明書を今回の検証結果へ更新し、`BUILD-INFO.json` に元ZIPとDLLのハッシュを保存しています。ソースのパッケージスクリプトも同じファイル選択へ修正しました。Windows OBS実機でのロード・カメラ・D3D11描画は引き続き未検証です。

## macOSで実際に確認した環境

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

- Windows 11のOBS実機でのDLLロード・インストールスクリプト実行
- Windows D3D11上のステージング転送、シェーダー、PNGのアルファ・色管理
- 実カメラの移動、傾き、近接・遠方、再接続
- Windows版OBS31/32/33の個別互換性
- 1280×720/30fps、60fpsの負荷・長時間配信・録画
- 複数フィルターチェーン、実機でのクロップ・左右反転
- 実OBS UIでの設定保存復元、終了・繰り返し削除、GPUメモリ増減

現段階は**Windows x64 DLL・導入用ZIPの生成、Windowsの数学テスト、macOS libobsでの合成試験を終えた納品**です。Windows OBS実機で完成条件を満たした「動作検証済み完成版」とは扱っていません。

## 開発中に修正したビルド問題

- OpenCVをサブプロジェクトとして組み込んだ際、未使用のOrbbecセンサー検出がトップ階層のパスを参照して構成に失敗。`WITH_OBSENSOR=OFF`で無効化。
- OpenCV生成ヘッダーがトップのバイナリディレクトリに置かれるため、そのディレクトリをinclude pathへ追加。
- ローカルarm64検証のOBSヘッダーにはSIMDeおよび生成obsconfig.hが必要だったため、検証用環境に用意。Windowsの公式SDK構成とは別。
- Windowsのランタイム不一致を避けるため、静的OpenCVにも `BUILD_WITH_STATIC_CRT=OFF`（/MD）を明示。Windowsの実ビルドで成功を確認。

- OBSテンプレートのディレクトリ共通 `UNICODE` / `_UNICODE` がOpenCVのANSI API呼び出しと衝突したため、OpenCVへ渡さずプラグインのターゲットに限定。
- MSVCの警告をエラー扱いにする数学テストで、小数定数3か所に `f` を付けて型変換警告を修正。
- OpenCVのインストール規則がSDKのヘッダー・静的ライブラリも配置したため、ZIP作成対象をプラグインとランタイムデータへ限定。

## グラフィックステストの再現（開発者向け）

`FPF_OBS_SMOKE=ON`でビルドした `obs-smoke` に以下の6引数を渡します。

```text
obs-smoke <プラグインバイナリ> <dataディレクトリ> <320x320 raw RGBA顔画像>
          <透過PNG> <libobs graphicsモジュール> <libobs effectデータディレクトリ>
```

テストは現在の作業ディレクトリへ `obs-smoke-output.rgba` を出力します。テスト用画像と出力画像は配布しません。WindowsではOBSのbin/64bitをDLL探索パスへ入れ、graphicsモジュールにlibobs-d3d11を指定したうえで別途検証してください。
