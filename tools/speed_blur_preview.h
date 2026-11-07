#pragma once
#include "nfsu/speed_blur_reference.h"
// Desktop reference; also used to compare one real Switch GPU frame. This
// preserves four channels and original constant-factor composition.
inline void speed_blur_preview(std::vector<unsigned char>& pixels,unsigned width,unsigned height,
                               const nfsu::SpeedBlurFrame& frame) {
    if(!frame.active())return;
    auto result=nfsu::diagnostic::speed_blur_reference(pixels,width,height,frame);
    if(!result.image[4].empty())pixels=std::move(result.image[4]);
}
