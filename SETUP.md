顔追従PNGオーバーレイ 0.1.0 ベータ — Windows 11 x64

【ダウンロードするもの】
face-png-filter-0.1.0-windows-x64.zip を選んでください。
「Source code」は開発者用です。ビルド済みDLLは入っていません。

【インストール】
1. ZIPを右クリックし「すべて展開」。ZIP内から直接実行しないでください。
2. OBSを終了します。
3. 展開先の INSTALL.cmd をダブルクリックします。
4. Windowsの管理者確認画面が出たら「はい」を選びます。
5. Installation complete と表示されたら、OBSを起動します。
標準の C:\Program Files\obs-studio へインストールします。
管理者確認に失敗する場合は INSTALL.cmd を右クリックして「管理者として実行」。
別の場所のOBSはREADMEの -OBSRoot の指定、または手動コピーをご利用ください。

【使い方】
1. Webカメラのソースを右クリック→フィルタ。
2. エフェクトフィルタの＋→「顔追従PNGオーバーレイ」。
3. PNGファイルを選び、追従位置を「鼻」にします。
4. 顔をカメラへ向け、拡大率や位置補正を調整します。
5. 確認用のマーカーや状態バーは配信・録画前にOFFにします。

【必要なもの】
Windows 11 x64、OBS Studio 31.1.1以上の64bit版、SDR映像、透過PNG。
フィルタ一覧に出ない／MSVCP140等のエラーはMicrosoft公式ランタイムを確認：
https://aka.ms/vc14/vc_redist.x64.exe
公式説明：https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist
Python、Visual Studio、OpenCVの別途インストールは不要です。
実行時のクラウド通信はありません。

【制限・不具合報告】
Windows x64のMSVCビルド・数学テストは成功済みです。
Windows OBS実機でのカメラ追従・D3D11描画・安定性は未検証のベータ版です。
1人のみ追従。額・頬・顎は5点から推定します。HDRは非対応です。
https://github.com/Ohsugi0101/obs-face-sticker/issues
報告にはOBSバージョン、Windows/GPU、再現手順、エラー内容を添えてください。
OBSログを公開する際はユーザー名・ファイルパス等を確認してください。

【削除】
OBS終了後、次のDLLと専用データフォルダーを削除します。
<OBS>\obs-plugins\64bit\face-png-filter.dll
<OBS>\data\obs-plugins\face-png-filter\

詳しい手順・ライセンスはREADME.mdとdocsをご覧ください。
