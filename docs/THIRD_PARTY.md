# ライセンス・再配布と固定バージョン

| 対象 | バージョン/取得元 | ライセンス・取り扱い |
|---|---|---|
| 本プラグイン | 0.1.0 | 新規コードはGPL-2.0-or-later。OpenCVとリンクした配布物全体はGPL-3.0-or-laterとして配布 |
| OBS公式テンプレート | `3e7d7ac3b5342cd7d9b88890b9c70b472d1520fc` | GPL-2.0-or-later。ルートLICENSEを保持。CMake基盤・ビルド用補助ファイルに利用 |
| libobs | Windows SDK 31.1.1（buildspec.jsonのハッシュ固定） | GPL-2.0-or-later。OBS本体のDLLは配布物に含めない |
| OpenCV | 4.10.0 | Apache-2.0。ソースを改変せず静的リンク。原文LICENSE、COPYRIGHT、同梱の第三者通知を `data/licenses/opencv-4.10.0/` に保持 |
| YuNet ONNX | opencv_zoo commit `48b10fa505aeb51fbd127fc60a5c660040e823b7` の2023marモデル | モデルディレクトリはMIT。再配布可能、`data/licenses/YuNet-LICENSE`を保持 |
| protobuf | OpenCV同梱3.19.1 | BSD系。OpenCV側の通知を保持 |
| zlib | OpenCV同梱版 | zlib License。同梱通知を保持 |
| flatbuffers | OpenCV同梱23.5.9 | Apache-2.0。通知を保持 |
| その他OpenCV内部コード | OpenCVのcore/imgproc/dnn/objdetectと依存calib3d/features2d/flann | OpenCVのCOPYRIGHTおよび各3rdparty通知参照。対象外機能の通知も保全のため含める。非free機能、IPP、OpenCL、ITTは無効 |
| MSVCランタイム | Windows上のMicrosoftパッケージ | この配布物には同梱しない。Microsoft公式再頒布可能パッケージから導入 |

Apache-2.0とGPLv2-onlyの組み合わせには互換性がなく、GPLv2-or-laterの「later」を利用してGPLv3の条件で配布します。根拠：[Apache Software Foundationの互換性説明](https://apache.org/licenses/GPL-compatibility.html)。GPLv3原文は `data/licenses/GPL-3.0.txt` に含めています。

バイナリを第三者へ配布する場合は、ライセンス文と通知だけでなく、そのバイナリに対応する本プロジェクトのソース・ビルドスクリプト、および静的リンクする依存の対応ソースを提供してください。OpenCVソースの固定URL・ハッシュは `cmake/OpenCV.cmake`、OBS SDK等は `buildspec.json` にあります。本納品物は本プロジェクトのソースZIPで、OpenCV全ソースを内包するものではありません。

## モデルとソースの完全性

- OpenCV 4.10.0 tar.gz SHA-256：`b2171af5be6b26f7a06b1229948bbb2bdaa74fcf5cd097e0af6378fce50a6eb9`
- YuNetモデル SHA-256：`8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4`
- YuNet LICENSE SHA-256：`c83b8120c50ccbd4c4f96edf53141bdd566ebb8f8e9227e415326aa1b1aba958`

モデル取得スクリプトはダウンロード完了・ハッシュ一致後に置換し、不一致データは使用しません。実行時には自動取得しません。モデルには個別の商用禁止条項を持つdlib/ibugの68点モデルを使用していません。
