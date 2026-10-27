#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace steamvr_gaze {
struct Eye {
    std::array<double,4> projection{};
    std::array<double,9> rotation{};
    std::array<double,3> translation{};
};
struct Calibration {
    double x=1, y=1, leftDegrees=0, rightDegrees=0;
};
using Vec3=std::array<double,3>;
using Mat3=std::array<double,9>;

inline Vec3 inverseTransform(const Mat3& rotation,const Vec3& translation,const Vec3& point) {
    Vec3 local{};
    for(int column=0;column<3;++column)
        for(int row=0;row<3;++row)
            local[column]+=rotation[row*3+column]*(point[row]-translation[row]);
    return local;
}

inline std::array<float,2> project(const Vec3& worldTarget,const Mat3& headRotation,
        const Vec3& headTranslation,const Eye& eye,const Calibration& calibration,int eyeIndex) {
    const auto& p=eye.projection;
    if(!(p[0]<0&&p[1]>0&&p[2]<0&&p[3]>0)) throw std::runtime_error("Invalid projection");
    const Vec3 head=inverseTransform(headRotation,headTranslation,worldTarget);
    const Vec3 local=inverseTransform(eye.rotation,eye.translation,head);
    if(!std::isfinite(local[0])||!std::isfinite(local[1])||!std::isfinite(local[2])||local[2]>=-.05)
        throw std::runtime_error("Invalid fixation point");
    constexpr double pi=3.141592653589793;
    const double yaw=(eyeIndex==0?calibration.leftDegrees:calibration.rightDegrees)*pi/180.;
    const double tx=std::tan(std::atan(local[0]/-local[2])*calibration.x+yaw);
    const double ty=(local[1]/-local[2])*calibration.y;
    const double x=2*(tx-p[0])/(p[1]-p[0])-1;
    const double y=2*ty/(p[3]-p[2])+(p[2]+p[3])/(p[3]-p[2]);
    if(!std::isfinite(x)||!std::isfinite(y)) throw std::runtime_error("Invalid screen point");
    return {float(std::clamp(x,-1.,1.)),float(std::clamp(y,-1.,1.))};
}
}
