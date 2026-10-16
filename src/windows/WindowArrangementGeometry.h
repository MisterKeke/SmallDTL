#pragma once

#include <QRect>
#include <QString>
#include <algorithm>
#include <cmath>

namespace WindowArrangementGeometry {

// Window rectangles and monitor work areas use the same native virtual-screen
// pixel coordinate space. Normalized values make saved layouts resilient to
// resolution changes and preserve negative monitor origins.
struct NormalizedRect { double x=0, y=0, width=1, height=1; };

inline NormalizedRect normalize(const QRect &rect, const QRect &workArea)
{
    if (workArea.width() <= 0 || workArea.height() <= 0) return {};
    return {(double(rect.x())-workArea.x())/workArea.width(),
            (double(rect.y())-workArea.y())/workArea.height(),
            double(rect.width())/workArea.width(),
            double(rect.height())/workArea.height()};
}

inline QRect denormalize(const NormalizedRect &rect, const QRect &workArea)
{
    if (workArea.width() <= 0 || workArea.height() <= 0 ||
        !std::isfinite(rect.x) || !std::isfinite(rect.y) ||
        !std::isfinite(rect.width) || !std::isfinite(rect.height) ||
        rect.width <= 0 || rect.height <= 0) return {};
    const int width=qRound(std::clamp(rect.width*workArea.width(),1.0,double(workArea.width())));
    const int height=qRound(std::clamp(rect.height*workArea.height(),1.0,double(workArea.height())));
    const int x=workArea.x()+qRound(std::clamp(rect.x*workArea.width(),0.0,double(workArea.width()-width)));
    const int y=workArea.y()+qRound(std::clamp(rect.y*workArea.height(),0.0,double(workArea.height()-height)));
    return QRect(x,y,width,height);
}

inline NormalizedRect placement(const QString &kind)
{
    if(kind==QStringLiteral("leftHalf"))return {0,0,0.5,1};
    if(kind==QStringLiteral("rightHalf"))return {0.5,0,0.5,1};
    if(kind==QStringLiteral("topLeft"))return {0,0,0.5,0.5};
    if(kind==QStringLiteral("topRight"))return {0.5,0,0.5,0.5};
    if(kind==QStringLiteral("bottomLeft"))return {0,0.5,0.5,0.5};
    if(kind==QStringLiteral("bottomRight"))return {0.5,0.5,0.5,0.5};
    if(kind==QStringLiteral("center"))return {0.2,0.15,0.6,0.7};
    return {0,0,1,1};
}

} // namespace WindowArrangementGeometry
