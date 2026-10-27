#include "Localization.h"
#include <iostream>

int main() {
    if (language::Text(L"視線補正") != L"視線補正") return 1;
    language::english = true;
    if (language::Text(L"選択した経路に切替") != L"Switch to selected source") return 16;
    if (language::Text(L"Baballonia OSC: バックグラウンドで受信中\r\nOSCパケット 12 / 視線値 48") !=
        L"Baballonia OSC: background listener running\r\nOSC packets 12 / gaze values 48") return 17;
    if (language::Text(L"視線補正") != L"Gaze calibration") return 2;
    if (language::Text(L"視線補正を反映") != L"Apply calibration") return 7;
    if (language::Text(L"0.1度刻み（-5～+5）") != L"0.1° steps (-5 to +5)") return 14;
    if (language::Text(L"左右の角度は-5～+5度で指定してください。") !=
        L"Use an eye offset from -5° to +5°.") return 15;
    if (language::Text(L"色計算：1.15倍（＋15.0%）") != L"Pixel work: 1.15x (+15.0%)") return 8;
    if (language::Text(L"● 要再起動 = 対象アプリのみ。保存時に確認します。") !=
        L"● Restart required for the target app. You will be asked before saving.") return 3;
    if (language::Text(L"導入状態: 実行ファイル単体です。視線補正と共通プロバイダーは未接続です。") !=
        L"Setup: Standalone EXE. Gaze calibration and the common provider are not connected.") return 4;
    if (language::Text(L"Pimax経路（純正優先）") != L"Pimax path (native priority)") return 9;
    if (language::Text(L"Pimax経路: NVIDIAプロバイダー停止中\r\n入力候補: SRanibro → Pimaxブリッジ（視線値は未計測）\r\nTunerのOSC転送は使用しません。") !=
        L"Pimax path: NVIDIA provider stopped\r\nInput candidate: SRanibro to Pimax bridge (gaze not measured)\r\nTuner OSC forwarding is not used.") return 10;
    if (language::Text(L"共通プロバイダー停止中。Start-SRanibro.cmdを実行。") !=
        L"Common provider stopped. Run Start-SRanibro.cmd.") return 11;
    if (language::Text(L"視線入力元の選択を次回起動用に保存できませんでした。") !=
        L"The gaze-source selection could not be saved for the next launch.") return 12;
    if (language::Text(L"PICO（Bridge 0.6.0+直接購読）") !=
        L"PICO (direct Bridge 0.6.0+)") return 16;
    if (language::Text(L"PICOヘッドセットのIPv4アドレスを入力して保存してください。") !=
        L"Enter and save the PICO headset IPv4 address.") return 17;
    if (language::Text(L"Pimax経路が現在動作中です。共通経路のプロファイルは適用できません。\n共通経路へ切り替える場合は、先にVRChatとPimax側プロバイダーを通常終了してください。") !=
        L"The Pimax path is running. A common-path profile cannot be applied.\nTo switch to the common path, close VRChat and the Pimax provider normally first.") return 13;
    const auto status = language::Text(L"保存値: オン / 大きさ 50 / 縦横比 1");
    for (wchar_t ch : status) if (ch >= 0x3040 && ch <= 0x9fff) return 5;
    language::english = false;
    if (language::Text(L"画質設定を保存") != L"画質設定を保存") return 6;
    std::cout << "LOCALIZATION_PASS ja en dynamic_status" << std::endl;
    return 0;
}
