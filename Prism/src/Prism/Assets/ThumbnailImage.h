#pragma once

// ============================================================================
// ThumbnailImage.h
// Matematica de miniaturas (so std): reduz uma imagem RGBA8 para caber em
// 'maxSide' pixels mantendo a proporcao (media por area, ponderada pelo alfa
// para nao escurecer bordas transparentes) e empacota/desempacota o resultado
// para o DiskCache. Nunca amplia.
// ============================================================================

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Prism {

    struct ThumbnailPixels {
        uint32_t Width = 0, Height = 0;
        std::vector<uint8_t> RGBA; // Width * Height * 4
    };

    namespace ThumbnailImage {

        inline void ComputeSize(uint32_t srcW, uint32_t srcH, uint32_t maxSide, uint32_t& outW, uint32_t& outH) {
            if (srcW == 0 || srcH == 0 || maxSide == 0) { outW = outH = 0; return; }
            if (srcW <= maxSide && srcH <= maxSide) { outW = srcW; outH = srcH; return; }
            if (srcW >= srcH) {
                outW = maxSide;
                outH = std::max<uint32_t>(1, (uint32_t)((uint64_t)srcH * maxSide / srcW));
            } else {
                outH = maxSide;
                outW = std::max<uint32_t>(1, (uint32_t)((uint64_t)srcW * maxSide / srcH));
            }
        }

        // 'rgba' = srcW*srcH*4 bytes. Entrada invalida -> imagem vazia.
        inline ThumbnailPixels Downscale(const uint8_t* rgba, uint32_t srcW, uint32_t srcH, uint32_t maxSide) {
            ThumbnailPixels result;
            if (!rgba)
                return result;
            ComputeSize(srcW, srcH, maxSide, result.Width, result.Height);
            if (result.Width == 0)
                return result;
            result.RGBA.resize((size_t)result.Width * result.Height * 4);

            for (uint32_t y = 0; y < result.Height; y++) {
                const uint32_t y0 = (uint32_t)((uint64_t)y * srcH / result.Height);
                const uint32_t y1 = std::max(y0 + 1, (uint32_t)((uint64_t)(y + 1) * srcH / result.Height));
                for (uint32_t x = 0; x < result.Width; x++) {
                    const uint32_t x0 = (uint32_t)((uint64_t)x * srcW / result.Width);
                    const uint32_t x1 = std::max(x0 + 1, (uint32_t)((uint64_t)(x + 1) * srcW / result.Width));
                    uint64_t r = 0, g = 0, b = 0, a = 0, count = 0;
                    for (uint32_t sy = y0; sy < y1 && sy < srcH; sy++) {
                        for (uint32_t sx = x0; sx < x1 && sx < srcW; sx++) {
                            const uint8_t* p = rgba + ((size_t)sy * srcW + sx) * 4;
                            r += (uint64_t)p[0] * p[3]; g += (uint64_t)p[1] * p[3]; b += (uint64_t)p[2] * p[3];
                            a += p[3];
                            count++;
                        }
                    }
                    uint8_t* out = &result.RGBA[((size_t)y * result.Width + x) * 4];
                    if (count == 0 || a == 0) {
                        out[0] = out[1] = out[2] = out[3] = 0;
                    } else {
                        out[0] = (uint8_t)(r / a); out[1] = (uint8_t)(g / a); out[2] = (uint8_t)(b / a);
                        out[3] = (uint8_t)(a / count);
                    }
                }
            }
            return result;
        }

        // [u32 largura][u32 altura][pixels]
        inline std::vector<uint8_t> Pack(const ThumbnailPixels& image) {
            std::vector<uint8_t> out(8 + image.RGBA.size());
            std::memcpy(out.data(), &image.Width, 4);
            std::memcpy(out.data() + 4, &image.Height, 4);
            if (!image.RGBA.empty())
                std::memcpy(out.data() + 8, image.RGBA.data(), image.RGBA.size());
            return out;
        }

        inline bool Unpack(const std::vector<uint8_t>& data, ThumbnailPixels& out, uint32_t maxSide) {
            if (data.size() < 8)
                return false;
            uint32_t w = 0, h = 0;
            std::memcpy(&w, data.data(), 4);
            std::memcpy(&h, data.data() + 4, 4);
            if (w == 0 || h == 0 || w > maxSide || h > maxSide)
                return false;
            if (data.size() != 8 + (size_t)w * h * 4)
                return false;
            out.Width = w; out.Height = h;
            out.RGBA.assign(data.begin() + 8, data.end());
            return true;
        }

    }

}
