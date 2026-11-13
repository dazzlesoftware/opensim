/*
 * Copyright (c) Contributors, http://opensimulator.org/
 * See CONTRIBUTORS.TXT for a full list of copyright holders.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the OpenSimulator Project nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Freely adapted from the Aurora version of the terrain compressor.
 * Copyright (c) Contributors, http://aurora-sim.org/, http://opensimulator.org/
 * Aurora version created from libOpenMetaverse Library terrain compressor
 */

#include "opensim/scene/terrain_layer.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <limits>
namespace opensim::scene {
namespace {
struct Tables {
    std::array<float, 256> quant;
    std::array<int, 256> copy;
    Tables() {
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                quant[y * 16 + x] = .125f / (1.f + 2.f * (x + y));
        bool diag = false, right = true;
        int x = 0, y = 0, n = 0;
        while (x < 16 && y < 16) {
            copy[y * 16 + x] = n++;
            if (!diag) {
                if (right) {
                    if (x < 15)
                        x++;
                    else
                        y++;
                    right = false;
                    diag = true;
                } else {
                    if (y < 15)
                        y++;
                    else
                        x++;
                    right = true;
                    diag = true;
                }
            } else if (right) {
                x++;
                y--;
                if (x == 15 || y == 0)
                    diag = false;
            } else {
                x--;
                y++;
                if (y == 15 || x == 0)
                    diag = false;
            }
        }
    }
};
const Tables tables;
// Match unchecked CLR conversion/negation without C++ float-cast or signed-overflow UB.
int source_int(float f) {
    if (!std::isfinite(f) || static_cast<double>(f) >= 2147483648. || static_cast<double>(f) < -2147483648.)
        return std::numeric_limits<int>::min();
    return static_cast<int>(f);
}
int source_negative(int v) {
    return std::bit_cast<int>(0u - std::bit_cast<unsigned>(v));
}
struct Bits {
    std::vector<std::uint8_t> data;
    std::size_t count = 0;
    void byte(std::uint32_t v, int n = 8) {
        for (int i = n - 1; i >= 0; --i) {
            if (count / 8 == data.size())
                data.push_back(0);
            data[count / 8] |= ((v >> i) & 1) << (7 - count % 8);
            ++count;
        }
    }
    void bits(std::uint32_t v, int n) {
        while (n > 8) {
            byte(v & 255);
            v >>= 8;
            n -= 8;
        }
        if (n)
            byte(v, n);
    }
    void real(float f) {
        bits(std::bit_cast<std::uint32_t>(f), 32);
    }
    std::size_t byte_pos() const {
        return count ? (count - 1) / 8 : 0;
    }
};
const float C16_1R = 0.35185093438159561476f * 2.82842712474619f;
const float C16_1I = 0.03465429229977286565f * 2.82842712474619f;
const float C16_2R = 0.34675996133053686546f * 2.82842712474619f;
const float C16_2I = 0.06897484482073575308f * 2.82842712474619f;
const float C16_3R = 0.33832950029358816957f * 2.82842712474619f;
const float C16_3I = 0.10263113188058934529f * 2.82842712474619f;
const float C16_4R = 0.32664074121909413196f * 2.82842712474619f;
const float C16_4I = 0.13529902503654924610f * 2.82842712474619f;
const float C16_5R = 0.31180625324666780814f * 2.82842712474619f;
const float C16_5I = 0.16666391461943662432f * 2.82842712474619f;
const float C16_6R = 0.29396890060483967924f * 2.82842712474619f;
const float C16_6I = 0.19642373959677554532f * 2.82842712474619f;
const float C16_7R = 0.27330046675043937206f * 2.82842712474619f;
const float C16_7I = 0.22429189658565907106f * 2.82842712474619f;
const float C16_8R = 0.25f * 2.82842712474619f;
const float W16_4R = 0.92387953251128675613f;
const float W16_4I = 0.38268343236508977173f;
const float W16_8R = 0.70710678118654752440f;

void dct16x16(float* a, int* iout, int& wbits) {
    float tmp[256];

    float x0r, x0i, x1r, x1i, x2r, x2i, x3r, x3i;
    float x4r, x4i, x5r, x5i, x6r, x6i, x7r, x7i;
    float xr, xi;
    float ftmp;

    int itmp;
    int j, k;
    int indx;

    const int maxwbits = 17; // per header encoding
    int wbitsMaxValue = 1 << wbits;
    bool dowbits = wbits < 17;

    const float* fQuantizeTable16 = tables.quant.data();
    const int* fCopyMatrix16 = tables.copy.data();
    {
        for (j = 0, k = 0; j < 256; j += 16, k++) {
            x4r = a[0 + j] - a[15 + j];
            xr = a[0 + j] + a[15 + j];
            x4i = a[8 + j] - a[7 + j];
            xi = a[8 + j] + a[7 + j];
            x0r = xr + xi;
            x0i = xr - xi;
            x5r = a[2 + j] - a[13 + j];
            xr = a[2 + j] + a[13 + j];
            x5i = a[10 + j] - a[5 + j];
            xi = a[10 + j] + a[5 + j];
            x1r = xr + xi;
            x1i = xr - xi;
            x6r = a[4 + j] - a[11 + j];
            xr = a[4 + j] + a[11 + j];
            x6i = a[12 + j] - a[3 + j];
            xi = a[12 + j] + a[3 + j];
            x2r = xr + xi;
            x2i = xr - xi;
            x7r = a[6 + j] - a[9 + j];
            xr = a[6 + j] + a[9 + j];
            x7i = a[14 + j] - a[1 + j];
            xi = a[14 + j] + a[1 + j];
            x3r = xr + xi;
            x3i = xr - xi;
            xr = x0r + x2r;
            xi = x1r + x3r;
            tmp[k] = C16_8R * (xr + xi);          //
            tmp[8 * 16 + k] = C16_8R * (xr - xi); //
            xr = x0r - x2r;
            xi = x1r - x3r;
            tmp[4 * 16 + k] = C16_4R * xr - C16_4I * xi;  //
            tmp[12 * 16 + k] = C16_4R * xi + C16_4I * xr; //
            x0r = W16_8R * (x1i - x3i);
            x2r = W16_8R * (x1i + x3i);
            xr = x0i + x0r;
            xi = x2r + x2i;
            tmp[2 * 16 + k] = C16_2R * xr - C16_2I * xi;  //
            tmp[14 * 16 + k] = C16_2R * xi + C16_2I * xr; //
            xr = x0i - x0r;
            xi = x2r - x2i;
            tmp[6 * 16 + k] = C16_6R * xr - C16_6I * xi;  //
            tmp[10 * 16 + k] = C16_6R * xi + C16_6I * xr; //
            xr = W16_8R * (x6r - x6i);
            xi = W16_8R * (x6i + x6r);
            x6r = x4r - xr;
            x6i = x4i - xi;
            x4r += xr;
            x4i += xi;
            xr = W16_4I * x7r - W16_4R * x7i;
            xi = W16_4I * x7i + W16_4R * x7r;
            x7r = W16_4R * x5r - W16_4I * x5i;
            x7i = W16_4R * x5i + W16_4I * x5r;
            x5r = x7r + xr;
            x5i = x7i + xi;
            x7r -= xr;
            x7i -= xi;
            xr = x4r + x5r;
            xi = x5i + x4i;
            tmp[16 + k] = C16_1R * xr - C16_1I * xi;      //
            tmp[15 * 16 + k] = C16_1R * xi + C16_1I * xr; //
            xr = x4r - x5r;
            xi = x5i - x4i;
            tmp[7 * 16 + k] = C16_7R * xr - C16_7I * xi; //
            tmp[9 * 16 + k] = C16_7R * xi + C16_7I * xr; //
            xr = x6r - x7i;
            xi = x7r + x6i;
            tmp[5 * 16 + k] = C16_5R * xr - C16_5I * xi;  //
            tmp[11 * 16 + k] = C16_5R * xi + C16_5I * xr; //
            xr = x6r + x7i;
            xi = x7r - x6i;
            tmp[3 * 16 + k] = C16_3R * xr - C16_3I * xi;  //
            tmp[13 * 16 + k] = C16_3R * xi + C16_3I * xr; //
        }

        for (j = 0, k = 0; j < 256; j += 16, k++) {
            x4r = tmp[0 + j] - tmp[15 + j];
            xr = tmp[0 + j] + tmp[15 + j];
            x4i = tmp[8 + j] - tmp[7 + j];
            xi = tmp[8 + j] + tmp[7 + j];
            x0r = xr + xi;
            x0i = xr - xi;
            x5r = tmp[2 + j] - tmp[13 + j];
            xr = tmp[2 + j] + tmp[13 + j];
            x5i = tmp[10 + j] - tmp[5 + j];
            xi = tmp[10 + j] + tmp[5 + j];
            x1r = xr + xi;
            x1i = xr - xi;
            x6r = tmp[4 + j] - tmp[11 + j];
            xr = tmp[4 + j] + tmp[11 + j];
            x6i = tmp[12 + j] - tmp[3 + j];
            xi = tmp[12 + j] + tmp[3 + j];
            x2r = xr + xi;
            x2i = xr - xi;
            x7r = tmp[6 + j] - tmp[9 + j];
            xr = tmp[6 + j] + tmp[9 + j];
            x7i = tmp[14 + j] - tmp[1 + j];
            xi = tmp[14 + j] + tmp[1 + j];
            x3r = xr + xi;
            x3i = xr - xi;
            xr = x0r + x2r;
            xi = x1r + x3r;

            //tmp[0 + k] = C16_8R * (xr + xi); //
            ftmp = C16_8R * (xr + xi);
            itmp = source_int(ftmp * fQuantizeTable16[k]);
            iout[fCopyMatrix16[k]] = itmp;

            if (dowbits) {
                if (itmp < 0)
                    itmp = source_negative(itmp);
                while (itmp > wbitsMaxValue) {
                    wbits++;
                    wbitsMaxValue = 1 << wbits;
                    if (wbits == maxwbits) {
                        dowbits = false;
                        break;
                    }
                }
            }

            //tmp[8 * Constants.TerrainPatchSize + k] = C16_8R * (xr - xi); //
            ftmp = C16_8R * (xr - xi);
            indx = 8 * 16 + k;
            itmp = source_int(ftmp * fQuantizeTable16[indx]);
            iout[fCopyMatrix16[indx]] = itmp;

            if (dowbits) {
                if (itmp < 0)
                    itmp = source_negative(itmp);
                while (itmp > wbitsMaxValue) {
                    wbits++;
                    wbitsMaxValue = 1 << wbits;
                    if (wbits == maxwbits) {
                        dowbits = false;
                        break;
                    }
                }
            }

            xr = x0r - x2r;
            xi = x1r - x3r;

            //tmp[4 * Constants.TerrainPatchSize + k] = C16_4R * xr - C16_4I * xi; //
            ftmp = C16_4R * xr - C16_4I * xi;
            indx = 4 * 16 + k;
            itmp = source_int(ftmp * fQuantizeTable16[indx]);
            iout[fCopyMatrix16[indx]] = itmp;

            if (dowbits) {
                if (itmp < 0)
                    itmp = source_negative(itmp);
                while (itmp > wbitsMaxValue) {
                    wbits++;
                    wbitsMaxValue = 1 << wbits;
                    if (wbits == maxwbits) {
                        dowbits = false;
                        break;
                    }
                }
            }

            //tmp[12 * Constants.TerrainPatchSize + k] = C16_4R * xi + C16_4I * xr;  //
            ftmp = C16_4R * xi + C16_4I * xr;
            indx = 12 * 16 + k;
            itmp = source_int(ftmp * fQuantizeTable16[indx]);
            iout[fCopyMatrix16[indx]] = itmp;

            if (dowbits) {
                if (itmp < 0)
                    itmp = source_negative(itmp);
                while (itmp > wbitsMaxValue) {
                    wbits++;
                    wbitsMaxValue = 1 << wbits;
                    if (wbits == maxwbits) {
                        dowbits = false;
                        break;
                    }
                }
            }

            x0r = W16_8R * (x1i - x3i);
            x2r = W16_8R * (x1i + x3i);
            xr = x0i + x0r;
            xi = x2r + x2i;

            //tmp[2 * Constants.TerrainPatchSize + k] = C16_2R * xr - C16_2I * xi;  //
            ftmp = C16_2R * xr - C16_2I * xi;
            indx = 2 * 16 + k;
            itmp = source_int(ftmp * fQuantizeTable16[indx]);
            iout[fCopyMatrix16[indx]] = itmp;

            if (dowbits) {
                if (itmp < 0)
                    itmp = source_negative(itmp);
                while (itmp > wbitsMaxValue) {
                    wbits++;
                    wbitsMaxValue = 1 << wbits;
                    if (wbits == maxwbits) {
                        dowbits = false;
                        break;
                    }
                }
            }

            //tmp[14 * Constants.TerrainPatchSize + k] = C16_2R * xi + C16_2I * xr;  //
            ftmp = C16_2R * xi + C16_2I * xr;
            indx = 14 * 16 + k;
            itmp = source_int(ftmp * fQuantizeTable16[indx]);
            iout[fCopyMatrix16[indx]] = itmp;

            if (dowbits) {
                if (itmp < 0)
                    itmp = source_negative(itmp);
                while (itmp > wbitsMaxValue) {
                    wbits++;
                    wbitsMaxValue = 1 << wbits;
                    if (wbits == maxwbits) {
                        dowbits = false;
                        break;
                    }
                }
            }

            xr = x0i - x0r;
            xi = x2r - x2i;

            //tmp[6 * Constants.TerrainPatchSize + k] = C16_6R * xr - C16_6I * xi;  //
            ftmp = C16_6R * xr - C16_6I * xi;
            indx = 6 * 16 + k;
            itmp = source_int(ftmp * fQuantizeTable16[indx]);
            iout[fCopyMatrix16[indx]] = itmp;

            if (dowbits) {
                if (itmp < 0)
                    itmp = source_negative(itmp);
                while (itmp > wbitsMaxValue) {
                    wbits++;
                    wbitsMaxValue = 1 << wbits;
                    if (wbits == maxwbits) {
                        dowbits = false;
                        break;
                    }
                }
            }

            //tmp[10 * Constants.TerrainPatchSize + k] = C16_6R * xi + C16_6I * xr; //
            ftmp = C16_6R * xi + C16_6I * xr;
            indx = 10 * 16 + k;
            itmp = source_int(ftmp * fQuantizeTable16[indx]);
            iout[fCopyMatrix16[indx]] = itmp;

            if (dowbits) {
                if (itmp < 0)
                    itmp = source_negative(itmp);
                while (itmp > wbitsMaxValue) {
                    wbits++;
                    wbitsMaxValue = 1 << wbits;
                    if (wbits == maxwbits) {
                        dowbits = false;
                        break;
                    }
                }
            }

            xr = W16_8R * (x6r - x6i);
            xi = W16_8R * (x6i + x6r);
            x6r = x4r - xr;
            x6i = x4i - xi;
            x4r += xr;
            x4i += xi;
            xr = W16_4I * x7r - W16_4R * x7i;
            xi = W16_4I * x7i + W16_4R * x7r;
            x7r = W16_4R * x5r - W16_4I * x5i;
            x7i = W16_4R * x5i + W16_4I * x5r;
            x5r = x7r + xr;
            x5i = x7i + xi;
            x7r -= xr;
            x7i -= xi;
            xr = x4r + x5r;
            xi = x5i + x4i;

            //tmp[1 * Constants.TerrainPatchSize + k] = C16_1R * xr - C16_1I * xi;  //
            ftmp = C16_1R * xr - C16_1I * xi;
            indx = 16 + k;
            itmp = source_int(ftmp * fQuantizeTable16[indx]);
            iout[fCopyMatrix16[indx]] = itmp;

            if (dowbits) {
                if (itmp < 0)
                    itmp = source_negative(itmp);
                while (itmp > wbitsMaxValue) {
                    wbits++;
                    wbitsMaxValue = 1 << wbits;
                    if (wbits == maxwbits) {
                        dowbits = false;
                        break;
                    }
                }
            }

            //tmp[15 * Constants.TerrainPatchSize + k] = C16_1R * xi + C16_1I * xr;  //
            ftmp = C16_1R * xi + C16_1I * xr;
            indx = 15 * 16 + k;
            itmp = source_int(ftmp * fQuantizeTable16[indx]);
            iout[fCopyMatrix16[indx]] = itmp;

            if (dowbits) {
                if (itmp < 0)
                    itmp = source_negative(itmp);
                while (itmp > wbitsMaxValue) {
                    wbits++;
                    wbitsMaxValue = 1 << wbits;
                    if (wbits == maxwbits) {
                        dowbits = false;
                        break;
                    }
                }
            }

            xr = x4r - x5r;
            xi = x5i - x4i;

            //tmp[7 * Constants.TerrainPatchSize + k] = C16_7R * xr - C16_7I * xi;  //
            ftmp = C16_7R * xr - C16_7I * xi;
            indx = 7 * 16 + k;
            itmp = source_int(ftmp * fQuantizeTable16[indx]);
            iout[fCopyMatrix16[indx]] = itmp;

            if (dowbits) {
                if (itmp < 0)
                    itmp = source_negative(itmp);
                while (itmp > wbitsMaxValue) {
                    wbits++;
                    wbitsMaxValue = 1 << wbits;
                    if (wbits == maxwbits) {
                        dowbits = false;
                        break;
                    }
                }
            }

            //tmp[9 * Constants.TerrainPatchSize + k] = C16_7R * xi + C16_7I * xr;  //
            ftmp = C16_7R * xi + C16_7I * xr;
            indx = 9 * 16 + k;
            itmp = source_int(ftmp * fQuantizeTable16[indx]);
            iout[fCopyMatrix16[indx]] = itmp;

            if (dowbits) {
                if (itmp < 0)
                    itmp = source_negative(itmp);
                while (itmp > wbitsMaxValue) {
                    wbits++;
                    wbitsMaxValue = 1 << wbits;
                    if (wbits == maxwbits) {
                        dowbits = false;
                        break;
                    }
                }
            }

            xr = x6r - x7i;
            xi = x7r + x6i;

            //tmp[5 * Constants.TerrainPatchSize + k] = C16_5R * xr - C16_5I * xi;  //
            ftmp = C16_5R * xr - C16_5I * xi;
            indx = 5 * 16 + k;
            itmp = source_int(ftmp * fQuantizeTable16[indx]);
            iout[fCopyMatrix16[indx]] = itmp;

            if (dowbits) {
                if (itmp < 0)
                    itmp = source_negative(itmp);
                while (itmp > wbitsMaxValue) {
                    wbits++;
                    wbitsMaxValue = 1 << wbits;
                    if (wbits == maxwbits) {
                        dowbits = false;
                        break;
                    }
                }
            }

            //tmp[11 * Constants.TerrainPatchSize + k] = C16_5R * xi + C16_5I * xr;  //
            ftmp = C16_5R * xi + C16_5I * xr;
            indx = 11 * 16 + k;
            itmp = source_int(ftmp * fQuantizeTable16[indx]);
            iout[fCopyMatrix16[indx]] = itmp;

            if (dowbits) {
                if (itmp < 0)
                    itmp = source_negative(itmp);
                while (itmp > wbitsMaxValue) {
                    wbits++;
                    wbitsMaxValue = 1 << wbits;
                    if (wbits == maxwbits) {
                        dowbits = false;
                        break;
                    }
                }
            }

            xr = x6r + x7i;
            xi = x7r - x6i;

            //tmp[3 * Constants.TerrainPatchSize + k] = C16_3R * xr - C16_3I * xi;  //
            ftmp = C16_3R * xr - C16_3I * xi;
            indx = 3 * 16 + k;
            itmp = source_int(ftmp * fQuantizeTable16[indx]);
            iout[fCopyMatrix16[indx]] = itmp;

            if (dowbits) {
                if (itmp < 0)
                    itmp = source_negative(itmp);
                while (itmp > wbitsMaxValue) {
                    wbits++;
                    wbitsMaxValue = 1 << wbits;
                    if (wbits == maxwbits) {
                        dowbits = false;
                        break;
                    }
                }
            }

            //tmp[13 * Constants.TerrainPatchSize + k] = C16_3R * xi + C16_3I * xr;  //
            ftmp = C16_3R * xi + C16_3I * xr;
            indx = 13 * 16 + k;
            itmp = source_int(ftmp * fQuantizeTable16[indx]);
            iout[fCopyMatrix16[indx]] = itmp;

            if (dowbits) {
                if (itmp < 0)
                    itmp = source_negative(itmp);
                while (itmp > wbitsMaxValue) {
                    wbits++;
                    wbitsMaxValue = 1 << wbits;
                    if (wbits == maxwbits) {
                        dowbits = false;
                        break;
                    }
                }
            }
        }
    }
}

void encode_coefficients(Bits& out, std::span<float, 256> block, float low, int range,
                         std::uint32_t id, bool large) {
    for (float f : block)
        if (!std::isfinite(f))
            throw std::invalid_argument("nonfinite terrain sample");
    int coeff[256];
    int wbits = 5;
    dct16x16(block.data(), coeff, wbits);
    wbits = std::clamp(wbits, 2, 17);
    out.byte(128 | (wbits - 2));
    out.real(low);
    out.bits(range, 16);
    out.bits(id, large ? 32 : 10);
    int maximum = (1 << wbits) - 1;
    for (int i = 0; i < 256;) {
        int v = coeff[i];
        if (v == 0) {
            int j = i + 1;
            while (j < 256 && coeff[j] == 0)
                j++;
            if (j == 256) {
                out.bits(2, 2);
                return;
            }
            while (i < j) {
                out.byte(0, std::min(8, j - i));
                i += std::min(8, j - i);
            }
            continue;
        }
        out.bits(v < 0 ? 7 : 6, 3);
        out.bits(std::min(v < 0 ? source_negative(v) : v, maximum), wbits);
        i++;
    }
}
void encode(Bits& out, const Terrain& t, TerrainPatchCoordinate p) {
    auto [low, high] = t.patch_min_max(p.x, p.y);
    const float frange = (high - low) + 1.f;
    if (!std::isfinite(frange) || frange < 1.f || frange >= 2147483648.f || !std::isfinite(low))
        throw std::invalid_argument("terrain patch range is not encodable");
    const int range = static_cast<int>(frange);
    const bool large = t.width() > 256 || t.height() > 256;
    const std::uint32_t id = large ? (p.x << 16) | (p.y & 65535) : (p.x << 5) | (p.y & 31);
    if (std::nearbyint(static_cast<double>(frange) * 100.) / 100. == 1.) {
        out.byte(0);
        out.real(low - .5f);
        out.bits(1, 16);
        out.bits(id, large ? 32 : 10);
        out.bits(2, 2);
        return;
    }
    auto block = t.patch(p.x, p.y, .5f * range + low, 1024.f / range);
    encode_coefficients(out, block, low, range, id, large);
}
} // namespace
TerrainLayerPacket wind_layer_packet(std::span<const framework::Vector3, 256> wind) {
    constexpr std::uint8_t type = 55;
    Bits out;
    out.bits(264, 16);
    out.byte(16);
    out.byte(type);
    for (const auto axis : {&framework::Vector3::x, &framework::Vector3::y}) {
        std::array<float, 256> block;
        std::ranges::transform(wind, block.begin(), axis);
        const auto [minimum, maximum] = std::ranges::minmax(block);
        const float frange = (maximum - minimum) + 1.f;
        if (!std::isfinite(frange) || frange < 1.f || frange >= 2147483648.f || !std::isfinite(minimum))
            throw std::invalid_argument("wind patch range is not encodable");
        const int range = static_cast<int>(frange);
        const float sub = .5f * range + minimum;
        const float premult = (1.f / range) * 1024.f;
        for (auto& value : block)
            value = (value - sub) * premult;
        encode_coefficients(out, block, minimum, range, 0, false);
    }
    out.byte(97);
    return {type, std::move(out.data)};
}
std::vector<TerrainLayerPacket> terrain_layer_packets(const Terrain& terrain,
                                                      std::span<const TerrainPatchCoordinate> patches) {
    // Use a consistent height/taint snapshot across a stream, without consuming taints.
    Terrain t(terrain);
    std::uint8_t type = (t.width() > 256 || t.height() > 256) ? 77 : 76;
    std::vector<TerrainLayerPacket> result;
    Bits out;
    auto start = [&] {
        out = Bits{};
        out.bits(264, 16);
        out.byte(16);
        out.byte(type);
    };
    auto finish = [&] {
        out.byte(97);
        result.push_back({type, std::move(out.data)});
    };
    start();
    for (std::size_t i = 0; i < patches.size(); i++) {
        encode(out, t, patches[i]);
        if (out.byte_pos() + 10 > 900 && i + 1 < patches.size()) {
            finish();
            start();
        }
    }
    finish();
    return result;
}
} // namespace opensim::scene
