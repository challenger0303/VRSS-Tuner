# フェイストラッキングとの共存監査（2026-10-26）

ここでいう「共存」は、VRSS Tuner の視線入力を動かしても、VRCFaceTracking（VRCFT）などがアバターへ送る顔・目の表情が継続すること。視線値を取得できること、NVIDIA に渡せること、VRChat 内で高画質領域が動くことは別々に確認する。

| フェイストラッキング経路 | Tuner と共有するもの | 現時点の判定 | 運用・残る検証 |
| --- | --- | --- | --- |
| SRanibro + VRCFT | SRanibro がカメラを取得し、VRCFT に表情を渡す。Tuner の共通経路は別の localhost:5555 視線ストリームを読む | Dream Air で視線の VRSS2 追従は既に実機確認。今回、VRCFT の顔パラメータとの同時継続は未測定 | SRanibro を停止せず、VRCFT の顔・目パラメータ更新を先に確認してから Tuner 経路を起動。Pimax 純正 XR5 カメラ経路との同時占有は別問題 |
| Baballonia + VRCFT | Tuner が localhost:9020 の OSC を受信し、任意で 8888 に原データを転送。正規化視線は 5556 へ送信 | コード上は入力と転送が別ポート。顔表情のパススルーはコード確認、同時実測は未実施 | Baballonia の送信先を Tuner に向ける場合、VRCFT へ転送するなら 8888 を設定。既存の OSC 送信元を二重に VRCFT に入れない |
| VIVE Pro Eye + VRCFT SRanipal module | 双方が同じ SRanipal ランタイムを使用 | 同時 SDK クライアント可否は未確認 | ベンダーランタイム起動、VRCFT eye/face 更新、Tuner 視線更新、Tuner 停止後の更新を実機で測る。VRCFT の SRanipal module には他モジュールとの起動競合報告あり |
| VIVE Focus Vision / Focus 3 / XR Elite の Streaming + VRCFT | VIVE Streaming SDK を複数クライアントが使う可能性 | 同時利用・開始終了の所有権は未確認 | VRCFT の顔値を維持したまま Tuner bridge を起動・終了して比較。Tuner 自体は VRChat/VRCFT へ OSC を送らない |
| PSVR2 Toolkit + VRCFT module / Baballonia | Tuner は CAPI、旧 VRCFT module は Toolkit の legacy IPC サーバーを使用 | 分離したインターフェースだが、同時 CAPI セッションは未確認 | Tuner の起動前確認はエクスポート検査だけにし、余分な CAPI init/deinit を避けた。実機で顔・視線の連続性を測る |
| PICO + UnifiedPicoModule / PicoFacialBridge | PC 側 UDP 9030 と headset の単一クライアントセッション | **同時に直接接続すると競合する** | 独立診断プローブは明示的 `--exclusive-test` 指定かつ VRCFT 停止時だけ動く。共存には UnifiedPicoModule 側からの視線複製・リレーが必要。PICO はまだ Tuner の VRSS2 入力ではない |
| Varjo + VRCFT | Varjo Native SDK セッション | Varjo 公式は描画アプリと別の視線解析アプリの同時利用をサポート。Tuner + VRCFT の実機同時テストは未実施 | Varjo bridge は現在試作で、Tuner 配布物には未収録。SDK 同時接続は有望だが顔値と VRSS2 描画は別途検証 |
| Quest Pro / Galaxy XR の Virtual Desktop + VRCFT | Virtual Desktop の共有状態を読み取り | 試作プローブは共有メモリの読み取りだけで、ポート・カメラの所有権を取らない | データ更新の鮮度判定と Tuner への視線変換は未実装。ストリーミング側の foveation と VRSS2 を混同しない |
| Quest Pro / Steam Frame の Steam Link + VRCFT | SteamVR/OpenVR の視線 action | 試作プローブは API 読み取りのみ。装着・VRCFT 同時実機値は未確認 | action が VRChat 起動中にも更新されるかを測ってから Tuner 入力にする |

今回の PC の読み取り検査では VRCFT、SRanibro、Baballonia、VIVE ランタイム、PICO bridge は起動しておらず、5555/5556/8888/9000/9020/9030 番の待受もなかった。VarjoProcessServer と VirtualDesktop.Service は起動中だったが、これだけではフェイストラッキングのデータ送出は証明できない。したがって上表の「未確認」は実際の同時運転に合格した意味ではない。

実機での共存判定は同じ装着状態・アバターで、(1) Tuner 停止中の VRCFT 顔・目パラメータ更新、(2) Tuner 視線経路起動中も同じ更新が続くか、(3) Tuner 停止後も回復不要で続くか、(4) VRSS2 高画質領域が追従するか、を連続して見る。カメラやベンダーサービスを停止・再起動しての「成功」は同時共存の証拠にしない。顔画像や生視線を保存する必要はなく、更新有無と時刻だけで判定できる。

根拠: [PICO BridgeSplit プロトコル](https://github.com/WolalaQAQ/UnifiedPicoModule/blob/main/docs/protocol.md)、[Varjo の multi-app support](https://developer.varjo.com/docs/get-started/multi-app-support)、[PSVR2 Toolkit 導入手順](https://github.com/BnuuySolutions/PSVR2Toolkit/wiki/Installation)、[SRanibro ガイド](https://github.com/challenger0303/SRanibro/blob/main/USER_GUIDE.md)、[VRCFT SRanipal module の起動競合報告](https://github.com/VRCFaceTracking/SRanipalTrackingModule/issues/11)。各実装については [HMD 対応状況](HMD-GAZE-COMPATIBILITY.md) も参照。
