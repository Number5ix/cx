// The render view's frames on macOS, presented with Metal as a renderer would: bands of color
// copied into the drawable from a buffer, never through cxui.

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "gallery.h"

void galleryMetalFrame(void* nativeLayer, int32 frame)
{
    static id<MTLDevice> device;
    static id<MTLCommandQueue> queue;
    if (!device) {
        device = MTLCreateSystemDefaultDevice();
        queue  = [device newCommandQueue];
    }
    CAMetalLayer* layer = (__bridge CAMetalLayer*)nativeLayer;
    if (!device || !layer)
        return;
    layer.device          = device;
    layer.pixelFormat     = MTLPixelFormatBGRA8Unorm;
    // So the drawable can take a copy
    layer.framebufferOnly = NO;
    id<CAMetalDrawable> drawable = [layer nextDrawable];
    if (!drawable)
        return;

    id<MTLTexture> tex = drawable.texture;
    NSUInteger w = tex.width, h = tex.height;
    id<MTLBuffer> buf  = [device newBufferWithLength:w * h * 4 options:MTLResourceStorageModeShared];
    uint32* px         = buf.contents;
    for (NSUInteger y = 0; y < h; y++) {
        uint32 shade = (uint32)((y * 16 / h) * 10 + frame * 3) % 150;
        uint32 bgra  = 0xff000000u | (32u << 16) | ((64 + shade / 2) << 8) | (96 + shade);
        for (NSUInteger x = 0; x < w; x++)
            px[y * w + x] = bgra;
    }

    id<MTLCommandBuffer> cmd    = [queue commandBuffer];
    id<MTLBlitCommandEncoder> b = [cmd blitCommandEncoder];
    [b copyFromBuffer:buf
             sourceOffset:0
        sourceBytesPerRow:w * 4
      sourceBytesPerImage:w * h * 4
               sourceSize:MTLSizeMake(w, h, 1)
                toTexture:tex
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:MTLOriginMake(0, 0, 0)];
    [b endEncoding];
    [cmd presentDrawable:drawable];
    [cmd commit];
}
