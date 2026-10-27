# VRSS Tuner

[English](README.md) · [日本語セットアップガイド](docs/SETUP.ja.md) · [English setup guide](docs/SETUP.md)

VRSS Tunerは、NVIDIA VRSS2の設定と、左右の視線データを共通VRSSプロバイダーへ送るためのWindows用試験ツールです。BaballoniaのOSC視線データを受け取り、OpenVRのHMD投影情報を使って左右別に位置を計算します。VRChatのファイル変更やゲームへのDLL注入は行いません。

**実機報告：Pimax Dream Air（開発者・NVIDIA 610.88）、Steam Frame（PR貢献者・NVIDIA 596.02）。** その他のHMDやPCは未確認です。NVIDIA、Pimax、Valve、VRChatの公式製品ではありません。

PSVR2の直接経路に加え、VIVE Pro EyeのSRanipal経路とVIVE Streaming経路を試験的に追加しました。PICOは受信プロトコル診断、VarjoはSDKブリッジの模擬試験までです。[HMD別の対応状況](docs/HMD-GAZE-COMPATIBILITY.md)に検証範囲を記載しています。Frame専用経路以外の新しいHMDでのVRSS追従は未確認です。

インストーラーにはTuner、共通視線プロバイダー、OpenVR投影情報取得ツール、日英ガイド、読み取り専用の導入診断を同梱しています。監査済みNVIDIAドライバーの組み込み契約は標準経路が610.88、Frame専用経路が596.02です。別の実体は[実験用ドライバー契約](docs/DRIVER-CONTRACTS.md)を明示したときだけ試せます。Frame専用経路は596.02のままです。VRSSを許可するVRChatプロファイル、SteamVR、視線入力、VRChat内のMSAAも必要です。共通プロバイダーは非公開のドライバーインターフェースを使うため、契約と一致しないドライバーでの起動を拒否します。診断合格だけではHMD内の視線追従を証明できません。

## Steam Frame（596.02・実験対応）

Frameでは旧来のEDID診断やStart-Providerを使わず、[Frame専用手順](docs/STEAM-FRAME.md)を使ってください。SteamVRの視線共有を有効にし、VRレイアウト4・MSAAを設定します。VRChatとUnityを閉じてTunerの「Steam Frame」→「接続 / 再試行」を押し、登録後に「VRChatを起動（Frame用）」を使います。上下変換は内蔵済みなので共通補正の初期値はX=1・Y=1です。元のPimax/Baballonia経路は維持しています。

## 使い始める

1. [Releases](https://github.com/challenger0303/VRSS-Tuner/releases)からWindowsインストーラーを取得します。ユーザー用の固定場所に配置し、SteamVRが既に起動していれば自動起動を登録します。インストーラー自身はSteamVRを起動しません。ポータブルZIPは開発・手動診断用に残します。
2. 標準経路ではHMDを接続してSteamVRを起動し、Tunerで視線入力元を選んで「視線経路を起動」を押します。導入診断とプロバイダー起動を行い、VRChatは操作しません。Steam Frameでは上記の専用手順を使います。
3. SteamからVRChatを通常起動し、ゲーム内のMSAAを有効にして[日本語セットアップガイド](docs/SETUP.ja.md)に沿って受信を確認します。

固定HMD IDをSteamの通常のVRChat起動で渡す場合は、`Install-Steam-Launch-Bridge.cmd`が示す起動オプションを一度設定します。インストーラーはブリッジを固定場所に配置します。VRChatが起動中の更新では使用中のブリッジを保持し、VRChat終了後の次回SteamVR起動時に更新します。このPCではSteamVRからTunerとSRanibro経路が自動起動し、通常起動したVRChatへの接続まで確認しました。他のPCは未確認です。

ドライバー非対応やVRSS許可設定の不足が表示された場合、強制起動しないでください。診断結果、ドライバー版、HMD機種を添えてIssueへ報告できます。個人情報は投稿前に削除してください。

## ソースからのビルド

Visual Studio 2022の「C++によるデスクトップ開発」とInno Setup 6を入れてください。PowerShellから`vrss-tuner\test.ps1`で独立テスト、`vrss-tuner\Build-ReleaseCandidate.ps1`でテスト・ZIP内ファイル検証・インストーラー作成を一括実行できます。`Build-Portable.ps1`は開発用ZIPを作ります。ビルドツールは`vswhere`で検出し、必要なら`VCVARS64_BAT`で指定できます。NVAPIとOpenVRのビルド用ファイルは各ライセンス表示とともに`third_party`にあります。

本プロジェクトのコードはMITライセンスです。[LICENSE](LICENSE)と[第三者ライセンス](THIRD_PARTY_NOTICES.md)を参照してください。NVIDIAとValveのコンポーネントはそれぞれのライセンスに従います。

共通プロバイダー内の入力切替はNVIDIAへの登録を維持します。完全な停止は視線プロバイダーを終了するため、再開時にVRChatの通常再起動が必要な場合があります。Baballoniaの受信はTunerを閉じても継続します。beta.5の検証結果と制約はセットアップガイドを参照してください。
