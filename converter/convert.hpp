#pragma once
#include <QString>
#include <functional>
#include <atomic>
namespace woody {
struct Options {
    QString bin, cue, output;
    int width=480, height=272, fps=60, detail=1, distance=100,memory=0,particles=128,textures=0,filter=1;
    bool outline=false,music=false;
};
using Progress=std::function<void(int,const QString&)>;
QString convert(const Options&, const Progress&, const std::atomic_bool& cancel);
void selfTest();
}
