# VRSS Tuner ポータブル版

NVIDIA VRSS2の設定と視線入力を扱うWindows用の試験版です。ZIPを任意の書き込み可能なフォルダーに展開してください。日本語・英語はTuner右上で切り替えられます。

**開発者の確認環境はPimax Dream Air**です（GeForce RTX 5090／NVIDIAドライバー610.88）。貢献者が整理後のSteam Frame経路も596.02で実機確認しています。Frameは下記の専用手順を使ってください。[詳細な日本語ガイド](https://github.com/challenger0303/VRSS-Tuner/blob/v0.1.0-beta.5/docs/SETUP.ja.md)も参照してください。

## 入力元の切替と停止

「視線入力」で入力元を選び、**選択した経路に切替**（停止中は「視線経路を起動」）を押します。別の経路が稼働中でも切り替えられます。切替は視線経路だけに行い、VRChatやSteamVRを再起動しません。Pimaxから共通経路へ切り替えるときは、純正カメラやフェイストラッキングを止めず、PimaxのVRSS視線送信だけを閉じます。

**停止**は選択欄に関係なく稼働中の視線経路を停止します。Tunerを開き直した後も検出でき、古い実行情報が残っていれば実行中PIDと新しい状態通知から探し直します。停止後はSteamVR連動で自動再接続せず、起動ボタンを押すまで待機します。停止に失敗した場合はエラーを表示し、停止済みとは表示しません。

共通プロバイダー内の入力切替はNVIDIAへの登録とPIDを保ったまま行います。完全な「停止」はプロバイダー自体を終了します。起動中のVRChatがNVIDIAの共有接続情報を保持している場合、完全停止後の再開にはVRChatの通常終了・再起動が必要です。起動エラーを成功として扱ったり、見えない常駐プロバイダーを残したりはしません。

## Baballonia OSC

1. Tunerで「Baballonia OSC」を選択します。受信ポートの初期値は **9020**、VRCFaceTrackingへの転送先は **8888** です。転送不要なら0にします。
2. BaballoniaのOSC送信先を **127.0.0.1:9020**（Tunerで別のポートを指定した場合はその番号）に変更し、Eye出力を有効にします。Baballoniaの既定の8888へ送ったままではTunerの9020へは届きません。
3. **視線経路を起動／選択した経路に切替**を押します。OSC受信はバックグラウンドの共通プロバイダーが担当するので、Tunerの画面を閉じても続きます。
4. 「OSCパケット」と「視線値」を確認します。パケットが0なら送信先・ポート・送信の有効化を確認し、パケットだけ増えるならEye出力とパラメーターを確認します。受信ポートを他のアプリと共有しないでください。競合時にはポート番号とエラーを表示します。

対応する視線名はLeftEyeX／LeftEyeY／RightEyeX／RightEyeYです。カスタムOSCプレフィックス、通常のOSCメッセージと入れ子のbundleを受け付けます。左右4軸の値が150ms以内に更新されている場合だけ有効として扱い、古い視線を継続送信しません。顔のOSCは指定した転送先へそのまま渡します。

## 最初にすること

1. ZIPを**丸ごと展開**して`VRSS Tuner.exe`を開き、右上で日本語を選びます。NVIDIAドライバー、HMDの視線ソフト、SteamVR、VRChatは別途必要です。
2. **Pimax純正XR5**なら、Pimaxの視線機能を有効にし、Tunerで「Pimax経路（純正優先）」と「VRSSオン」を選んで適用します。`Start-Provider.cmd`や共通経路用の`Test-Setup.cmd`は不要です。
3. **Steam Frame**なら、下のFrame専用手順を使います。共通経路の起動スクリプトは使いません。
4. **その他の視線入力**なら、HMDと視線ソフトを起動し、TunerでVRSSをオンにして入力元を選び、「視線経路を起動」を押します。共通プロバイダーを起動しますが、VRChatは起動・終了しません。必要ならSteam経由でSteamVRを起動します。Windowsの管理者確認だけは手動で承認してください。
5. VRChatのグラフィック設定でMSAAを有効にし、HMD内で両目の高画質領域が視線に追従するか確認します。Tunerの緑の表示だけでは画質の変化は確認できません。

視線経路を起動した後に、VRChatを自分で起動できます。固定ID構成では`Start-VRChat-HMD-Key.cmd`で後から起動するか、`Install-Steam-Launch-Bridge.cmd`の案内に従ってSteamのVRChat起動オプションを一度設定してください。通常起動ブリッジはVRChat起動時にHMDを照合し、固定IDの構成ではプロバイダーがまだ起動途中でもIDを予約します。後から視線経路を再接続できます。SteamVRとの同時起動にはインストーラー版を使ってください。インストーラーはSteamVRを起動せず、Steamから既に起動している場合だけ自動起動登録を行います。管理者確認が出たら手動で承認してください。

他のPCや未検証HMDでの動作は保証していません。共通経路では監査済みドライバー実体と、VRSSを許可するNVIDIAのVRChatプロファイルが必要です。診断が失敗した場合は、その理由を確認してください。

## Steam Frame（596.02・実験対応）

Frameでは旧来のEDID診断やStart-Providerを使わず、[Frame専用手順](STEAM-FRAME.md)を使ってください。SteamVRの視線共有を有効にし、VRレイアウト4・MSAAを設定します。VRChatとUnityを閉じてTunerの「Steam Frame」→「接続 / 再試行」を押し、登録後に「VRChatを起動（Frame用）」を使います。上下変換は内蔵済みなので共通補正の初期値はX=1・Y=1です。下記の汎用SteamVR視線アクション経路とは別です。
## 入力元を選ぶ

共通経路では、HMDと視線ソフトを起動し、TunerでVRSSをオンにしてから、使う入力元を一つ選び、起動／切替ボタンを押します。VRChatは自動で起動・終了しません。固定ID設定がVRChatの起動時に渡されていない場合は、対応する起動手順でVRChatを一度起動し直す必要があります。同じ共通プロバイダー内での入力切替は再起動なしで行えます。プロバイダーの完全停止後は、NVIDIA側に残る接続情報によりVRChatの再起動が必要な場合があります。Steam Frameは上記の専用経路を使います。

| 入力元 | Tunerで選ぶ項目 | 起動方法 |
| --- | --- | --- |
| Pimax純正XR5 | Pimax経路（純正優先） | Pimaxの通常の視線機能を使用。共通プロバイダーは不要 |
| Pimax + SRanibro CUSTOM | Pimax経路（純正優先） | Pimax視線ブリッジを導入した環境で使用。純正経路とは排他的 |
| Baballoniaの両目OSC | Baballonia OSC | Tunerの起動／切替ボタン、または`Start-Provider.cmd`を実行。OSC送信先は`127.0.0.1:9020` |
| PSVR2 Toolkit CAPI | PSVR2 Toolkit（直接） | 対応するToolkitを先に起動し、`Start-PSVR2-Toolkit.cmd`を実行。Toolkit本体は同梱しません |
| SRanibroの両目視線ストリーム | SRanibro（共通） | SRanibroを起動し、`Start-SRanibro.cmd`を実行。Pimax以外のHMDも同じ入口を使います |
| VIVE Pro Eye | VIVE Pro Eye（SRanipal・実機未検証） | SRanipalの視線を校正し、`Start-Vive-Pro-Eye.cmd`を実行。SRanipal本体は同梱しません |
| VIVE Focus VisionなどのPCストリーミング | VIVE Streaming（実機未検証） | VIVE Streamingの視線転送を有効にし、`Start-Vive-Streaming.cmd`を実行。SDK本体は同梱しません |
| Steam Link経由のQuest Proなどの実験用SteamVR入力 | SteamVR視線アクション（実機未検証） | PCへの視線共有を有効にして`Start-SteamVR-Eye.cmd`を実行。単一の注視点を左右へ投影します。Steam Frameには上記の専用タブを使います |
| Varjo XR-4 / Aero / XR-3 / VR-3 | Varjo Native SDK（実機未検証） | Varjo Baseを起動し、`Start-Varjo.cmd`を実行。インストール済み`VarjoLib.dll`が必要です |
| PICOの視線対応機 | PICO（Bridge 0.6.0+直接購読） | Bridge 0.6.0以降を起動し、TunerでヘッドセットIPv4を保存するか`Start-PICO.cmd`を実行。[PICO設定](PICO.md)を参照 |

Tunerの「視線経路を起動」ボタンは選択中の入力元に対応したプロバイダー起動スクリプトを開きます。BaballoniaではバックグラウンドのOSC受信と転送も開始します。`Start-Provider.cmd`は手動診断用です。VIVEのDLLが標準の導入先にないときは、`VRSS_VIVE_SDK_DLL`環境変数に`SRanipal.dll`または`VS_PC_SDK.dll`のフルパスを指定してください。VarjoのDLLが見つからない場合は`VRSS_VARJO_DLL`にインストール済み`VarjoLib.dll`のフルパスを指定します。ベンダーDLLは同梱しません。Pro Eyeは片目だけ有効なフレームでも、その視線方向を両目へ投影します。VIVE・SteamVR視線アクション・Varjoはコードと模擬入力での確認にとどまり、VRChat内の追従は未検証です。Varjoの倍率は起動時に読み込み、目別の角度オフセットはまだ反映しません。

視線入力タブの4段階は、選んだHMDだけでなく実際に稼働している経路を監視します。緑はその段階の観測成功、橙は起動中だが次の確認が必要、灰色は停止またはデータ待ちです。BaballoniaではOSC受信と転送、SRanibro・PSVR2 Toolkit・VIVE・SteamVR視線アクション・Varjoでは視線アプリ／ブリッジの起動と新しい視線が共通プロバイダーに届いたこと、さらにプロバイダー登録とNVIDIAの受理を別々に表示します。選択中と稼働中の経路が違えば、その両方を表示します。緑の最終段階もVRSSの画質変化そのものの証明ではありません。

下部の青い輪は視線の実座標、緑の点は表示専用の拡大位置です。「表示倍率」は1～4倍で、視線データ・補正値・VRSS領域に影響しません。共通経路ではVRChatがまだ視線を照会していなくても、プロバイダーに新しい視線が届けば点が動きます。その場合は「プロバイダー入力視線」と表示し、「NVIDIAが受理した視線」と区別します。Pimax経路では共有キューを読めるときはNVIDIA側の座標、読めないときはPimax SDKの入力座標を表示します。点は高画質領域の輪郭ではないため、最終的な描画はHMD内で確認してください。入力元を選んだ後は「選択した経路に切替」を押します。画質設定の変更にはVRChatの再起動が必要な場合があります。

共通経路の起動ではWindowsの管理者確認が出ます。VRChat起動後にMSAAを有効にし、Tuner下部で視線の受信とNVIDIAへの受理を確認してください。表示される点は送信した視線位置であり、高画質領域そのものではありません。終了時は`Stop-Provider.cmd`を実行します。Toolkit・VIVE・SteamVR・Varjoのブリッジも停止します。

監査済みNVIDIA HMDテスト経路が有効な共通プロバイダーでは、`Test-Setup.cmd`は接続用の固定キー`0xFEEE:0x0001`を使います。これは物理EDIDではなく、プロバイダーとVRChatを一致させるためだけの値です。投影情報は接続中HMDから別途取得し、HMDごとの視線補正も必要です。EDIDやシリアル番号がなくても投影情報を読めれば、IDの段階では停止しません。`Start-VRChat-HMD-Key.cmd`は起動時に同じキーを渡します。旧`Start-VRChat-Candidate.cmd`も互換入口として残しています。VRChatは対応するSteam起動ブリッジから後で起動できます。Steam Frameは専用経路を使います。

通常のSteam起動でIDを渡すには、インストール先の`Install-Steam-Launch-Bridge.cmd`を一度実行し、表示された起動オプションをSteamの「VRChat → プロパティ → 一般 → 起動オプション」に設定します。選択中の共通経路とHMDの識別情報を起動時に照合します。固定IDの場合は視線プロバイダーの起動前でもIDを予約でき、後から再接続できます。Pimax純正経路ではブリッジは通常起動をそのまま渡します。SteamVRの自動起動登録と保護付きVRChatでの固定IDは開発者のPCで確認しましたが、インストーラー後のSteamVR再起動と他のPCは未確認です。

手動実験には従来どおり`VRSS_HMD_ID_CANDIDATE`に十進数の`vendor:product`を指定できます。この場合は`candidate-manual`と表示され、VRChat起動時にも同じ環境変数が必要です。固定キーや手動候補IDは、他のHMDでVRSS2が動く保証ではありません。`consumerCount`、新しい視線の受理、HMD内のForce/Off画質差を順に確認してください。同じ固定キーの共通プロバイダーを複数同時には起動しません。

HMD別の実装・実機検証状況は[対応表](HMD-GAZE-COMPATIBILITY.md)にまとめています。

SRanibro共通経路はローカルのBrokenEye形式ストリーム`127.0.0.1:5555`を読み、接続中HMDのOpenVR投影情報とTunerの補正値を使います。HMDごとに視線座標系が一致するとは限りません。SRanibroがその形式の両目視線を出さない設定では利用できません。Pimax公式プロバイダーが動いている場合は、選択した共通経路への切替時にPimaxのVRSS送信だけを停止します。カメラ用サービスやフェイストラッキングアプリは停止しません。

VRCFaceTrackingなどとの共存条件は[フェイストラッキング共存監査](FACE-TRACKING-COEXISTENCE.md)を参照してください。PICOの単一クライアント視線プローブはVRCFTと同時に使えません。VIVE/PSVR2などの実機同時動作はまだ未確認です。

この全部入りリリース候補ZIPにはPimax視線ブリッジを`pimax-eye-bridge/driver_sranibro_eye.dll`に同梱します。自動では導入しません。片目だけ視線が有効なときは、その目の新しい方向を両目の入力に使います。両目とも無効、またはデータが古いときは視線を送信しません。この単眼時のHMD内の位置合わせは実機検証中です。Varjoブリッジも同梱しますが、Varjoランタイムは別途インストールが必要です。正確な内容とハッシュは`bundle-manifest.json`で確認してください。未検証HMDでの実機対応を保証するものではありません。SRanibro本体とNVIDIA/Pimaxのドライバーは同梱していません。

## 制約とファイル

共通プロバイダーには監査済みのドライバー実体が2組内蔵されています。別の版は`Audit-Driver.cmd`でDLLのハッシュとメソッド位置を非公開登録なしで採取できます。ABIを別途確認した上級者向けに、[DRIVER-CONTRACTS.md](DRIVER-CONTRACTS.md)の実験用契約ファイルで完全一致の実体を明示的に追加できます。未登録の実体は拒否します。VRChatプロファイルのVRSS許可フラグも必要です。MSAA、有効な両目視線、HMD別の補正が揃って初めて実機で確認できます。視線補正の初期値は`common-vrss-provider/calibration.txt`からコピーされ、変更後の値は`%LOCALAPPDATA%\VRSS Tuner\common-calibration.txt`に保持されます。診断とログも同じフォルダーに保存されます。

本プロジェクトのコードはMITライセンスです。OpenVR、NVAPI、PSVR2 Toolkitのヘッダーに適用される条件は同梱の各ライセンス文書を参照してください。NVIDIA、Pimax、Sony、Valve、VRChatの公式製品ではありません。

Varjo経路のゲインと左右オフセットも、補正ファイルの変更を実行中に読み込みます。実機での位置合わせと他ソフトとの同時使用は未検証です。
