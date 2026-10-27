#include <array>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include "third_party/openvr/openvr.h"
#include "common-vrss-provider/hmd_candidate_identity.h"

static std::string Property(vr::IVRSystem* system, vr::ETrackedDeviceProperty property) {
    std::array<char,512> value{};
    vr::ETrackedPropertyError error{};
    const auto length=system->GetStringTrackedDeviceProperty(0,property,value.data(),
        static_cast<uint32_t>(value.size()),&error);
    if(error!=vr::TrackedProp_Success || length<2 || length>value.size() ||
        value[length-1]!='\0') return {};
    return std::string(value.data(),length-1);
}

int wmain(int argc,wchar_t** argv) {
    const bool manual=argc==5 && std::wstring(argv[2])==L"--candidate-id";
    const bool session=argc==3 && std::wstring(argv[2])==L"--common-session-key";
    if(argc!=2 && !manual && !session) return 2;
    vr::EVRInitError error{};
    auto vrSystem=vr::VR_Init(&error,vr::VRApplication_Background);
    if(!vrSystem || error!=vr::VRInitError_None) return 3;
    vr::ETrackedPropertyError a{},b{};
    auto vid=vrSystem->GetInt32TrackedDeviceProperty(0,vr::Prop_EdidVendorID_Int32,&a);
    auto pid=vrSystem->GetInt32TrackedDeviceProperty(0,vr::Prop_EdidProductID_Int32,&b);
    const bool actualValid=a==vr::TrackedProp_Success && b==vr::TrackedProp_Success &&
        vid>0 && pid>0 && vid<=65535 && pid<=65535;
    const auto candidate=hmd_identity::Derive(
        Property(vrSystem,vr::Prop_TrackingSystemName_String),
        Property(vrSystem,vr::Prop_SerialNumber_String));
    auto source=std::string("openvr-edid");
    if(session) {
        vid=hmd_identity::kCandidateVendor;
        pid=hmd_identity::kCommonSessionProduct;
        source="tuner-session";
    } else if(manual) {
        try {
            size_t usedVendor=0,usedProduct=0;
            const auto requestedVendor=std::stoul(argv[3],&usedVendor,10);
            const auto requestedProduct=std::stoul(argv[4],&usedProduct,10);
            if(usedVendor!=std::wstring(argv[3]).size() || usedProduct!=std::wstring(argv[4]).size() ||
                requestedVendor<1 || requestedVendor>65535 || requestedProduct<1 || requestedProduct>65535)
                throw std::out_of_range("candidate identity");
            vid=static_cast<int32_t>(requestedVendor);pid=static_cast<int32_t>(requestedProduct);
        } catch(...) {vr::VR_Shutdown();std::cerr<<"INVALID_CANDIDATE_ID\n";return 6;}
        source="candidate-manual";
    } else if(!actualValid) {
        if(!candidate.valid) {
            vr::VR_Shutdown();std::cerr<<"HMD_EDID_AND_STABLE_ID_UNAVAILABLE vendor_error="<<int(a)
                <<" product_error="<<int(b)<<'\n';return 4;
        }
        vid=candidate.vendor;pid=candidate.product;
        source="candidate-auto";
    }
    std::ofstream out{std::filesystem::path(argv[1])}; out<<std::setprecision(10);
    out<<"format=1\nvendor="<<vid<<"\nproduct="<<pid<<"\nidentitySource="
       <<source<<"\n";
    if(candidate.valid)
        out<<"identityFingerprint="<<std::hex<<std::setw(16)<<std::setfill('0')
           <<candidate.fingerprint<<std::dec<<"\n";
    if(actualValid)out<<"actualVendor="<<vrSystem->GetInt32TrackedDeviceProperty(0,vr::Prop_EdidVendorID_Int32)
                      <<"\nactualProduct="<<vrSystem->GetInt32TrackedDeviceProperty(0,vr::Prop_EdidProductID_Int32)<<"\n";
    for(int i=0;i<2;++i) {
        float l,r,t,d;
        vrSystem->GetProjectionRaw(static_cast<vr::EVREye>(i),&l,&r,&t,&d);
        auto pose=vrSystem->GetEyeToHeadTransform(static_cast<vr::EVREye>(i));
        out<<"projection"<<i<<"="<<l<<","<<r<<","<<t<<","<<d<<"\n";
        out<<"eyeToHead"<<i<<"=";
        for(int row=0;row<3;++row) for(int col=0;col<3;++col)
            out<<(row||col?",":"")<<pose.m[row][col];
        out<<"\n";
        out<<"eyeTranslation"<<i<<"="<<pose.m[0][3]<<","<<pose.m[1][3]<<","<<pose.m[2][3]<<"\n";
    }
    std::cout<<"vendor="<<vid<<" product="<<pid<<" identitySource="
             <<source<<" runtime="<<vrSystem->GetRuntimeVersion()<<"\n";
    vr::VR_Shutdown();
    return out.good()?0:5;
}
