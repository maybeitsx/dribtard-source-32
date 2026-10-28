#include "ImGuiUI.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <filesystem>
#include <vector>
#include <wincodec.h>
#include <shlobj.h>
#include <shellapi.h>
#include <d3d11.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shell32.lib")

#include "../Input/HotkeyManager.hpp"
#include "../Version.hpp"

namespace
{
    // Full menu size. While the menu panel is closed the window shrinks to
    // just the icon rail (kRailWindowWidth).
    constexpr ImVec2 kWindowSize(820.0f, 524.0f);
    constexpr float kRailWindowWidth = 58.0f;
    constexpr float kCompactWindowWidth = 490.0f;
    constexpr float kCompactGeneralWidth = 560.0f;

    // Top edge of the icon rail and the panel, measured from the window top.
    constexpr float kPanelTop = 16.0f;

    /*
        The panel and cards are see-through so the game shows behind the menu,
        like the reference overlay. Raise the values for a more solid look.
    */
    constexpr float kPanelOpacity = 0.40f;
    constexpr float kCardOpacity = 0.72f;

    // Shared metrics for the footer row and the unload pill.
    constexpr float kFooterPillHeight = 24.0f;
    constexpr float kFooterMargin = 16.0f;
    constexpr float kFooterBottomMargin = 8.0f;
    constexpr float kUnloadGlyphReserve = 16.0f;

    // Continuous feathered mesh for the emblem. ImGui's DX11 blend state
    // premultiplies RGB during rendering, so vertex colours stay straight-alpha.
    void DrawSoftGlow(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding,
        float reach, const ImVec4& color, float strength)
    {
        if (strength <= 0.001f || max.x <= min.x || max.y <= min.y)
            return;

        const float radius = std::min(rounding, std::min(max.x - min.x, max.y - min.y) * 0.5f);
        constexpr int kCornerSegments = 10;
        constexpr int kPoints = 4 * (kCornerSegments + 1);
        constexpr int kBands = 8;
        ImVec2 rings[kBands + 1][kPoints];
        ImU32 colors[kBands + 1];
        const ImVec2 centers[4] = {
            ImVec2(max.x - radius, min.y + radius), ImVec2(max.x - radius, max.y - radius),
            ImVec2(min.x + radius, max.y - radius), ImVec2(min.x + radius, min.y + radius)
        };
        for (int band = 0; band <= kBands; ++band)
        {
            const float fraction = static_cast<float>(band) / kBands;
            const float distance = reach * fraction;
            // A Gaussian-like falloff, fading all the way to transparent.
            const float falloff = std::exp(-4.0f * fraction * fraction) * (1.0f - fraction);
            const float alpha = std::clamp(strength * 0.32f * falloff, 0.0f, 1.0f);
            colors[band] = ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, alpha));
            for (int corner = 0; corner < 4; ++corner)
                for (int point = 0; point <= kCornerSegments; ++point)
                {
                    const float angle = -IM_PI * 0.5f + corner * IM_PI * 0.5f +
                        (static_cast<float>(point) / kCornerSegments) * IM_PI * 0.5f;
                    rings[band][corner * (kCornerSegments + 1) + point] = ImVec2(
                        centers[corner].x + std::cos(angle) * (radius + distance),
                        centers[corner].y + std::sin(angle) * (radius + distance));
                }
        }

        const ImVec2 uv = draw->_Data->TexUvWhitePixel;
        for (int band = 0; band < kBands; ++band)
            for (int point = 0; point < kPoints; ++point)
            {
                const int next = (point + 1) % kPoints;
                draw->PrimReserve(6, 4);
                const ImDrawIdx base = static_cast<ImDrawIdx>(draw->_VtxCurrentIdx);
                draw->PrimWriteIdx(base); draw->PrimWriteIdx(base + 1); draw->PrimWriteIdx(base + 2);
                draw->PrimWriteIdx(base); draw->PrimWriteIdx(base + 2); draw->PrimWriteIdx(base + 3);
                draw->PrimWriteVtx(rings[band][point], uv, colors[band]);
                draw->PrimWriteVtx(rings[band][next], uv, colors[band]);
                draw->PrimWriteVtx(rings[band + 1][next], uv, colors[band + 1]);
                draw->PrimWriteVtx(rings[band + 1][point], uv, colors[band + 1]);
            }
    }

    void AddTooltip(const char* text)
    {
        if (ImGui::IsItemHovered() && ImGui::BeginTooltip())
        {
            ImGui::PushTextWrapPos(320.0f);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    // Keep an actual checkbox for keyboard navigation and test discovery, while
    // drawing a compact animated switch in its place.
    bool DrawModernToggle(const char* label, bool* v, float& animProgress, const char* tooltip)
    {
        ImGuiIO& io = ImGui::GetIO();
        const float dt = io.DeltaTime > 0.0f ? io.DeltaTime : (1.0f / 60.0f);

        const float target = *v ? 1.0f : 0.0f;
        const float step = std::min(1.0f, dt * 16.0f);
        animProgress += (target - animProgress) * step;

        const ImVec2 cursorScreen = ImGui::GetCursorScreenPos();
        const float width = 40.0f;
        const float height = 22.0f;

        char hiddenId[128];
        std::snprintf(hiddenId, sizeof(hiddenId), "##%s", label);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.001f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 3.0f));
        const bool changed = ImGui::Checkbox(hiddenId, v);
        ImGui::PopStyleVar(2);
#ifdef IMGUI_ENABLE_TEST_ENGINE
        if (GImGui && GImGui->TestEngineHookItems)
            ImGuiTestEngineHook_ItemInfo(GImGui, ImGui::GetItemID(), label, ImGui::GetItemStatusFlags());
#endif

        const bool hovered = ImGui::IsItemHovered();
        AddTooltip(tooltip);

        if (*v != (target > 0.5f))
        {
            const float newTarget = *v ? 1.0f : 0.0f;
            animProgress += (newTarget - animProgress) * 0.45f;
        }

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float t = std::clamp(animProgress, 0.0f, 1.0f);
        const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
        const ImVec2 trackMin(cursorScreen.x + 2.0f, cursorScreen.y + 2.0f);
        const ImVec2 trackMax(cursorScreen.x + width - 2.0f, cursorScreen.y + height - 2.0f);
        const ImVec4 off = hovered ? ImVec4(0.19f, 0.23f, 0.28f, 1.0f) : ImVec4(0.11f, 0.14f, 0.18f, 1.0f);
        const ImVec4 track(off.x + (accent.x - off.x) * t * 0.75f,
            off.y + (accent.y - off.y) * t * 0.75f,
            off.z + (accent.z - off.z) * t * 0.75f, 1.0f);
        draw->AddRectFilled(trackMin, trackMax, ImGui::GetColorU32(track), 9.0f);
        draw->AddRect(trackMin, trackMax,
            ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.18f + 0.32f * t)), 9.0f);
        const ImVec2 knob(trackMin.x + 9.0f + t * (trackMax.x - trackMin.x - 18.0f),
            (trackMin.y + trackMax.y) * 0.5f);
        if (t > 0.05f)
            draw->AddCircleFilled(knob, 8.0f, ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.20f * t)), 20);
        draw->AddCircleFilled(knob, 6.0f, IM_COL32(239, 247, 250, 255), 20);

        return changed;
    }

    // A consistent instrument-panel header shared by every feature card.
    void DrawOchoCardHeader(const char* title, float cardWidth, const ImVec4& accent)
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 winPos = ImGui::GetWindowPos();
        const float headerH = 25.0f;
        const ImVec2 hMin = winPos;
        const ImVec2 hMax(winPos.x + cardWidth, winPos.y + headerH);

        draw->AddRectFilled(hMin, hMax, IM_COL32(17, 25, 35, 205), 10.0f, ImDrawFlags_RoundCornersTop);

        const ImVec2 triCenter(winPos.x + 13.0f, winPos.y + headerH * 0.5f);
        const ImU32 accentCol = ImGui::GetColorU32(accent);
        draw->AddCircle(triCenter, 4.0f, accentCol, 16, 1.2f);
        draw->AddCircleFilled(triCenter, 1.5f, accentCol, 12);

        const ImVec2 textSz = ImGui::CalcTextSize(title);
        draw->AddText(
            ImVec2(winPos.x + 23.0f, winPos.y + (headerH - textSz.y) * 0.5f),
            IM_COL32(240, 241, 246, 255),
            title);

        const float dashY = winPos.y + headerH * 0.5f;
        for (int i = 0; i < 3; ++i)
            draw->AddCircleFilled(ImVec2(winPos.x + cardWidth - 20.0f + i * 5.0f, dashY),
                1.0f, IM_COL32(120, 151, 166, 150), 8);

        const float lineY = winPos.y + headerH;
        draw->AddLine(
            ImVec2(winPos.x + 1.0f, lineY),
            ImVec2(winPos.x + cardWidth - 1.0f, lineY),
            IM_COL32(255, 255, 255, 18),
            1.0f);

        draw->AddLine(ImVec2(winPos.x + 12.0f, lineY),
            ImVec2(winPos.x + 48.0f, lineY),
            ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.72f)), 1.5f);

        ImGui::SetCursorPosY(headerH + 8.0f);
    }

    void DrawSectionHeader(const char* title, int iconKind)
    {
        (void)title;
        (void)iconKind;
    }

    void DrawRowDivider(float width)
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        draw->AddLine(
            ImVec2(p.x + 12.0f, p.y),
            ImVec2(p.x + width - 12.0f, p.y),
            IM_COL32(255, 255, 255, 13), 1.0f);
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
    }

    // Animated 2D trajectory preview card inside Camera Movement that visualizes
    // the horizontal and vertical curve angles live (outward curve -> Q ability -> exact return),
    // and allows dragging the target point to interactively adjust custom horizontal & vertical angles.
    void DrawTrajectoryPreviewCard(
        float width,
        float height,
        float& smoothHoriz,
        float& smoothVert,
        MacroSettings& settings,
        const ImVec4& accent)
    {
        ImGuiIO& io = ImGui::GetIO();
        const float dt = io.DeltaTime > 0.0f ? io.DeltaTime : (1.0f / 60.0f);

        const ImVec2 cardMin = ImGui::GetCursorScreenPos();
        const ImVec2 cardMax(cardMin.x + width, cardMin.y + height);
        ImDrawList* draw = ImGui::GetWindowDrawList();

        // Recessed inner canvas with subtle border.
        draw->AddRectFilled(cardMin, cardMax, IM_COL32(8, 15, 23, 245), 8.0f);
        draw->AddRect(cardMin, cardMax, IM_COL32(255, 255, 255, 20), 8.0f);

        // Plot area below the mini header.
        const ImVec2 plotMin(cardMin.x + 10.0f, cardMin.y + 28.0f);
        const ImVec2 plotMax(cardMax.x - 10.0f, cardMax.y - 8.0f);
        const ImVec2 center(
            (plotMin.x + plotMax.x) * 0.5f,
            (plotMin.y + plotMax.y) * 0.5f + 6.0f);

        const float halfW = (plotMax.x - plotMin.x) * 0.42f;
        const float halfH = (plotMax.y - plotMin.y) * 0.42f;

        // Interactive canvas button for clicking/dragging the target point.
        ImGui::SetCursorScreenPos(plotMin);
        ImGui::InvisibleButton("##trajectory_canvas", ImVec2(plotMax.x - plotMin.x, plotMax.y - plotMin.y));
        const bool canvasHovered = ImGui::IsItemHovered();
        const bool canvasActive = ImGui::IsItemActive();

        if (canvasActive && (ImGui::IsMouseClicked(0) || ImGui::IsMouseDragging(0, 0.0f)))
        {
            if (!settings.customCurveAngle)
            {
                const auto& preset = kCurvePresets.at(static_cast<size_t>(settings.curveType));
                settings.secondaryDelaySeconds = preset.delayBeforePressingSeconds;
                settings.customCurveAngle = true;
            }

            const float relX = std::clamp((io.MousePos.x - center.x) / halfW, -1.0f, 1.0f);
            const float relY = std::clamp((center.y - io.MousePos.y) / halfH, -1.0f, 1.0f);
            settings.horizontalDegrees = std::round(static_cast<double>(relX) * 180.0);
            settings.verticalDegrees = std::round(static_cast<double>(relY) * 180.0);
            smoothHoriz = static_cast<float>(settings.horizontalDegrees);
            smoothVert = static_cast<float>(settings.verticalDegrees);
        }

        const MacroSettings effective = GetEffectiveCurveSettings(settings);
        const double targetHoriz = effective.horizontalDegrees;
        const double targetVert = effective.verticalDegrees;

        const float lerpStep = canvasActive ? 1.0f : std::min(1.0f, dt * 12.0f);
        smoothHoriz += (static_cast<float>(targetHoriz) - smoothHoriz) * lerpStep;
        smoothVert += (static_cast<float>(targetVert) - smoothVert) * lerpStep;

        // Header label and live angle readouts inside the preview card.
        draw->AddText(ImVec2(cardMin.x + 10.0f, cardMin.y + 7.0f), IM_COL32(190, 192, 202, 255), "2D Trajectory Preview");
        char readout[64];
        std::snprintf(readout, sizeof(readout), "%+.0f deg H  |  %+.0f deg V", targetHoriz, targetVert);
        const ImVec2 readoutSz = ImGui::CalcTextSize(readout);
        draw->AddText(
            ImVec2(cardMax.x - readoutSz.x - 10.0f, cardMin.y + 7.0f),
            ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.98f)),
            readout);

        draw->PushClipRect(plotMin, plotMax, true);

        // Subtle grid & radar rings.
        const ImU32 gridCol = IM_COL32(255, 255, 255, 11);
        const ImU32 axisCol = IM_COL32(255, 255, 255, 24);
        for (float gx = center.x - 150.0f; gx <= center.x + 150.0f; gx += 30.0f)
            draw->AddLine(ImVec2(gx, plotMin.y), ImVec2(gx, plotMax.y), gridCol, 1.0f);
        for (float gy = center.y - 60.0f; gy <= center.y + 60.0f; gy += 24.0f)
            draw->AddLine(ImVec2(plotMin.x, gy), ImVec2(plotMax.x, gy), gridCol, 1.0f);

        draw->AddLine(ImVec2(plotMin.x, center.y), ImVec2(plotMax.x, center.y), axisCol, 1.0f);
        draw->AddLine(ImVec2(center.x, plotMin.y), ImVec2(center.x, plotMax.y), axisCol, 1.0f);
        draw->AddCircle(center, 26.0f, IM_COL32(255, 255, 255, 16), 32, 1.0f);
        draw->AddCircle(center, 52.0f, IM_COL32(255, 255, 255, 10), 40, 1.0f);

        // Map (-180..180 H, -180..180 V) into plot coordinates. Positive vertical
        // pitches the camera upwards on screen (negative Y in screen space).
        const float normX = std::clamp(smoothHoriz / 180.0f, -1.0f, 1.0f);
        const float normY = std::clamp(smoothVert / 180.0f, -1.0f, 1.0f);
        const ImVec2 targetPt(center.x + normX * halfW, center.y - normY * halfH);

        // Curved control point so the outward camera flick arcs naturally.
        const float dx = targetPt.x - center.x;
        const float dy = targetPt.y - center.y;
        const ImVec2 ctrlOut(
            center.x + dx * 0.38f - dy * 0.16f,
            center.y + dy * 0.72f + dx * 0.10f);
        const ImVec2 ctrlRet(
            center.x + dx * 0.62f + dy * 0.14f,
            center.y + dy * 0.28f - dx * 0.08f);

        auto evalQuad = [](ImVec2 a, ImVec2 b, ImVec2 c, float u) -> ImVec2
        {
            const float inv = 1.0f - u;
            return ImVec2(
                inv * inv * a.x + 2.0f * inv * u * b.x + u * u * c.x,
                inv * inv * a.y + 2.0f * inv * u * b.y + u * u * c.y);
        };

        // Soft crimson glow & primary curve path.
        constexpr int kSegments = 24;
        ImVec2 prev = center;
        for (int i = 1; i <= kSegments; ++i)
        {
            const float u = static_cast<float>(i) / static_cast<float>(kSegments);
            const ImVec2 cur = evalQuad(center, ctrlOut, targetPt, u);
            draw->AddLine(prev, cur, ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.24f)), 5.0f);
            draw->AddLine(prev, cur, ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.92f)), 2.0f);
            prev = cur;
        }

        // Brushed silver return trajectory path showing camera restoration.
        prev = targetPt;
        for (int i = 1; i <= kSegments; ++i)
        {
            const float u = static_cast<float>(i) / static_cast<float>(kSegments);
            const ImVec2 cur = evalQuad(targetPt, ctrlRet, center, u);
            if (i % 2 == 1)
                draw->AddLine(prev, cur, IM_COL32(205, 208, 218, 85), 1.2f);
            prev = cur;
        }

        // Origin & interactive crosshair target reticles (styled like the 'd' logo crosshair).
        draw->AddCircleFilled(center, 3.5f, IM_COL32(225, 226, 234, 225), 16);
        const float targetRadius = (canvasHovered || canvasActive) ? 8.5f : 6.5f;
        draw->AddCircle(
            targetPt,
            targetRadius,
            ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, canvasActive ? 0.95f : 0.70f)),
            24,
            canvasActive ? 2.0f : 1.4f);
        draw->AddLine(
            ImVec2(targetPt.x - targetRadius - 2.5f, targetPt.y),
            ImVec2(targetPt.x + targetRadius + 2.5f, targetPt.y),
            ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.65f)), 1.2f);
        draw->AddLine(
            ImVec2(targetPt.x, targetPt.y - targetRadius - 2.5f),
            ImVec2(targetPt.x, targetPt.y + targetRadius + 2.5f),
            ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.65f)), 1.2f);
        draw->AddCircleFilled(targetPt, canvasActive ? 4.5f : 3.2f, ImGui::GetColorU32(accent), 16);

        // Animated live cursor cycling: outward curve (0..0.42), Q hold flash (0.42..0.56), return (0.56..0.90), pause.
        const float cycle = std::fmod(static_cast<float>(ImGui::GetTime()) * 0.75f, 1.0f);
        ImVec2 dotPos = center;
        float qFlash = 0.0f;
        if (cycle < 0.42f)
        {
            const float u = cycle / 0.42f;
            const float ease = u * u * (3.0f - 2.0f * u);
            dotPos = evalQuad(center, ctrlOut, targetPt, ease);
        }
        else if (cycle < 0.56f)
        {
            dotPos = targetPt;
            qFlash = 1.0f - std::fabs((cycle - 0.49f) / 0.07f);
        }
        else if (cycle < 0.90f)
        {
            const float u = (cycle - 0.56f) / 0.34f;
            const float ease = u * u * (3.0f - 2.0f * u);
            dotPos = evalQuad(targetPt, ctrlRet, center, ease);
        }

        if (qFlash > 0.0f)
        {
            draw->AddCircle(
                targetPt,
                7.0f + (1.0f - qFlash) * 10.0f,
                ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.80f * qFlash)),
                24, 1.8f);
        }

        draw->AddCircleFilled(
            dotPos, 6.5f,
            ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.35f)), 18);
        draw->AddCircleFilled(dotPos, 3.6f, IM_COL32(250, 250, 252, 255), 16);

        draw->PopClipRect();

        ImGui::SetCursorScreenPos(ImVec2(cardMin.x, cardMax.y));
        ImGui::Dummy(ImVec2(width, 0.0f));
        AddTooltip("Click or drag inside the 2D Trajectory Preview to interactively adjust custom Horizontal and Vertical curve angles.");
    }

    bool LoadTextureFromFile(
        const wchar_t* filename,
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        ID3D11ShaderResourceView** out_srv)
    {
        *out_srv = nullptr;

        IWICImagingFactory* factory = nullptr;
        IWICBitmapDecoder* decoder = nullptr;
        IWICBitmapFrameDecode* frame = nullptr;
        IWICFormatConverter* converter = nullptr;

        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory))))
            return false;

        bool ok = false;
        do
        {
            if (FAILED(factory->CreateDecoderFromFilename(filename, nullptr, GENERIC_READ,
                WICDecodeMetadataCacheOnLoad, &decoder))) break;
            if (FAILED(decoder->GetFrame(0, &frame))) break;
            if (FAILED(factory->CreateFormatConverter(&converter))) break;
            if (FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) break;

            UINT width = 0, height = 0;
            converter->GetSize(&width, &height);
            if (!width || !height) break;

            std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
            if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()))) break;

            /*
                Build the full mip chain. The logo source is far larger than
                the tile it is drawn into, so without mip levels the downscale
                aliases badly and looks grainy. Generating mips lets the
                trilinear sampler pick a small level and stay crisp.
            */
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = 0;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

            ID3D11Texture2D* texture = nullptr;
            HRESULT hr = device->CreateTexture2D(&desc, nullptr, &texture);
            if (SUCCEEDED(hr))
            {
                context->UpdateSubresource(texture, 0, nullptr, pixels.data(), width * 4, 0);

                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
                srvDesc.Format = desc.Format;
                srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels = static_cast<UINT>(-1);

                hr = device->CreateShaderResourceView(texture, &srvDesc, out_srv);

                if (SUCCEEDED(hr))
                    context->GenerateMips(*out_srv);

                texture->Release();
            }

            ok = SUCCEEDED(hr);
        } while (false);

        if (converter) converter->Release();
        if (frame) frame->Release();
        if (decoder) decoder->Release();
        factory->Release();
        return ok;
    }
}


bool ImGuiUI::Initialize(
    HWND hwnd,
    ID3D11Device* device,
    ID3D11DeviceContext* context)
{
    IMGUI_CHECKVERSION();

    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.FontGlobalScale = 1.0f;

    // Crisp text: rasterize the UI font with 3x3 oversampling so glyphs stay
    // smooth instead of pixelated, falling back to the built-in font.
    const char* fontPath = "C:\\Windows\\Fonts\\segoeui.ttf";
    ImFontConfig fontConfig;
    fontConfig.OversampleH = 3;
    fontConfig.OversampleV = 3;
    fontConfig.PixelSnapH = true;
    fontConfig.RasterizerMultiply = 1.05f;
    if (!io.Fonts->AddFontFromFileTTF(fontPath, 16.0f, &fontConfig))
        io.Fonts->AddFontDefault();

    ApplyTheme();

    // Load the supplied macro icon. Try the executable directory first, then the working directory.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::filesystem::path iconPath = std::filesystem::current_path() / "assets" / "macro_icon.png";
    if (!LoadTextureFromFile(iconPath.wstring().c_str(), device, context, &m_MacroIcon))
    {
        wchar_t modulePath[MAX_PATH]{};
        GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
        std::filesystem::path exeDir(modulePath);
        iconPath = exeDir.parent_path() / "assets" / "macro_icon.png";
        LoadTextureFromFile(iconPath.wstring().c_str(), device, context, &m_MacroIcon);
    }

    if (!ImGui_ImplWin32_Init(hwnd))
        return false;

    if (!ImGui_ImplDX11_Init(device, context))
    {
        ImGui_ImplWin32_Shutdown();
        return false;
    }

    m_Initialized = true;
    return true;
}

void ImGuiUI::ApplyTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowPadding = ImVec2(0.0f, 0.0f);
    // Compact controls fit the fixed-height cards with the 16px UI font.
    style.FramePadding = ImVec2(10.0f, 3.0f);
    style.ItemSpacing = ImVec2(8.0f, 4.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize = 8.0f;
    style.GrabMinSize = 8.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;

    style.AntiAliasedLines = true;
    style.AntiAliasedLinesUseTex = true;
    style.AntiAliasedFill = true;

    style.WindowRounding = 12.0f;
    style.ChildRounding = 10.0f;
    style.FrameRounding = 7.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;

    const ImVec4 accent = Accent();
    const ImVec4 accentBright(
        std::min(1.0f, accent.x + 0.09f),
        std::min(1.0f, accent.y + 0.08f),
        std::min(1.0f, accent.z + 0.08f),
        1.0f);

    style.Colors[ImGuiCol_WindowBg] = m_WindowColor;
    style.Colors[ImGuiCol_ChildBg] = m_BackgroundColor;
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.055f, 0.075f, 0.095f, 0.98f);
    style.Colors[ImGuiCol_FrameBg] = m_FrameColor;
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.14f, 0.20f, 0.24f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.16f, 0.25f, 0.29f, 1.0f);
    style.Colors[ImGuiCol_Button] = m_ButtonColor;
    style.Colors[ImGuiCol_ButtonHovered] = m_ButtonHoveredColor;
    style.Colors[ImGuiCol_ButtonActive] = accent;
    style.Colors[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.45f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(accentBright.x, accentBright.y, accentBright.z, 0.65f);
    style.Colors[ImGuiCol_HeaderActive] = accentBright;
    style.Colors[ImGuiCol_CheckMark] = accent;
    style.Colors[ImGuiCol_SliderGrab] = accent;
    style.Colors[ImGuiCol_SliderGrabActive] = accentBright;
    style.Colors[ImGuiCol_Border] = ImVec4(1.0f, 1.0f, 1.0f, 0.07f);
    style.Colors[ImGuiCol_Separator] = ImVec4(1.0f, 1.0f, 1.0f, 0.06f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.90f, 0.94f, 0.96f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.56f, 0.64f, 0.69f, 1.0f);
}

void ImGuiUI::UpdateAnimations()
{
    ImGuiIO& io = ImGui::GetIO();
    const float dt = io.DeltaTime > 0.0f ? io.DeltaTime : (1.0f / 60.0f);

    m_AppearAnim = std::min(1.0f, m_AppearAnim + dt * 3.2f);
}

void ImGuiUI::DrawLogoGlow(ImVec2 min, ImVec2 max, float rounding)
{
    if (m_GlowIntensity <= 0.001f)
        return;

    const float t = static_cast<float>(ImGui::GetTime()) * 2.2f;
    const float pulse = 0.74f + 0.26f * std::sin(t);
    const ImVec4 accent = Accent();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    DrawSoftGlow(draw, min, max, rounding, 9.0f, accent, m_GlowIntensity * pulse);
}

void ImGuiUI::DrawWindowGlow(ImVec2 min, ImVec2 max, float appear)
{
    const float intensity = m_WindowGlowIntensity * appear;
    if (intensity <= 0.001f)
        return;

    // Instead of a rectangular outer halo, wash a soft, animated accent light
    // into the top edge of the glass. The outer corners remain transparent.
    const ImVec4 accent = Accent();
    const float pulse = 0.82f + 0.18f * std::sin(static_cast<float>(ImGui::GetTime()) * 1.1f);
    const float alpha = std::clamp(intensity * pulse * 0.30f, 0.0f, 0.55f);
    const float width = max.x - min.x;
    const float inset = 18.0f;
    const float y = min.y + 1.0f;
    const float center = min.x + width * 0.50f;
    const ImU32 clear = ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.0f));
    const ImU32 light = ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, alpha));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilledMultiColor(ImVec2(min.x + inset, y), ImVec2(center, y + 20.0f),
        clear, light, clear, clear);
    draw->AddRectFilledMultiColor(ImVec2(center, y), ImVec2(max.x - inset, y + 20.0f),
        light, clear, clear, clear);
    draw->AddLine(ImVec2(center - std::min(width * 0.18f, 90.0f), y),
        ImVec2(center + std::min(width * 0.18f, 90.0f), y),
        ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, alpha * 1.5f)), 1.0f);
}

void ImGuiUI::RestoreAppearanceDefaults()
{
    m_AccentColor = ImVec4(0.24f, 0.84f, 0.88f, 1.0f);
    m_CardColor = ImVec4(0.055f, 0.075f, 0.095f, 1.0f);
    m_GlowIntensity = 0.45f;
    m_WindowGlowIntensity = 0.45f;
    m_PanelOpacity = kPanelOpacity;
    m_DarkenBackground = false;
    m_DimOpacity = 0.45f;
    ApplyTheme();
}

void ImGuiUI::DrawDimOverlay(float appear)
{
    if (!m_DarkenBackground)
        return;
    const float alpha = std::clamp(m_DimOpacity, 0.0f, 1.0f) * appear;
    if (alpha <= 0.001f)
        return;
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    if (displaySize.x <= 0.0f || displaySize.y <= 0.0f)
        return;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilled(
        ImVec2(0.0f, 0.0f),
        displaySize,
        ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, alpha)));
}

void ImGuiUI::SetProcessStatus(bool attached, DWORD pid)
{
    m_Attached = attached;
    m_Pid = pid;
}

void ImGuiUI::SetCapturedKey(
    int virtualKey,
    MacroSettings& settings,
    MoonwalkSettings& moonwalk)
{
    if (virtualKey == 0)
        return;

    switch (m_CaptureTarget)
    {
    case CaptureTarget::TriggerKey:
        settings.triggerKey = virtualKey;
        break;
    case CaptureTarget::ActivationKey:
        settings.activationKey = virtualKey;
        break;
    case CaptureTarget::SecondaryKey:
        settings.secondaryKey = virtualKey;
        break;
    case CaptureTarget::ToggleKey:
        // Keep emergency stop reserved and do not bind menu clicks.
        if (virtualKey == VK_F12 || virtualKey <= VK_XBUTTON2)
            return;
        m_ToggleKey = virtualKey;
        break;
    case CaptureTarget::MoonwalkHoldKey:
        // Emergency stop stays reserved; a mouse button cannot be held as a
        // keyboard macro keybind.
        if (virtualKey == VK_F12 || virtualKey <= VK_XBUTTON2)
            return;
        moonwalk.holdKey = virtualKey;
        break;
    default:
        break;
    }

    m_CaptureTarget = CaptureTarget::None;
}

bool ImGuiUI::IsCapturingKey() const
{
    return m_CaptureTarget != CaptureTarget::None;
}

UiSettings ImGuiUI::GetUiSettings() const
{
    UiSettings ui{};
    ui.toggleKey = m_ToggleKey;
    ui.accentR = m_AccentColor.x;
    ui.accentG = m_AccentColor.y;
    ui.accentB = m_AccentColor.z;
    ui.accentA = m_AccentColor.w;
    ui.cardR = m_CardColor.x;
    ui.cardG = m_CardColor.y;
    ui.cardB = m_CardColor.z;
    ui.glowIntensity = m_GlowIntensity;
    ui.windowGlowIntensity = m_WindowGlowIntensity;
    ui.panelOpacity = m_PanelOpacity;
    ui.darkenBackground = m_DarkenBackground;
    ui.dimOpacity = m_DimOpacity;
    ui.limitMenuFps = m_LimitMenuFps;
    ui.menuFpsCap = m_MenuFpsCap;
    return ui;
}

void ImGuiUI::SetUiSettings(const UiSettings& ui)
{
    if (ui.toggleKey >= 1 && ui.toggleKey <= 255)
        m_ToggleKey = ui.toggleKey;
    m_AccentColor = ImVec4(ui.accentR, ui.accentG, ui.accentB, ui.accentA);
    m_CardColor = ImVec4(ui.cardR, ui.cardG, ui.cardB, 1.0f);
    m_GlowIntensity = std::clamp(ui.glowIntensity, 0.0f, 1.5f);
    m_WindowGlowIntensity = std::clamp(ui.windowGlowIntensity, 0.0f, 1.5f);
    m_PanelOpacity = std::clamp(ui.panelOpacity, 0.30f, 1.00f);
    m_DarkenBackground = ui.darkenBackground;
    m_DimOpacity = std::clamp(ui.dimOpacity, 0.05f, 0.85f);
    m_LimitMenuFps = ui.limitMenuFps;
    m_MenuFpsCap = std::clamp(ui.menuFpsCap, kMinMenuFpsCap, kMaxMenuFpsCap);
    ApplyTheme();
}

void ImGuiUI::SetConfigList(const std::vector<std::string>& names)
{
    m_ConfigList = names;
    if (!m_SelectedConfig.empty() &&
        std::find(m_ConfigList.begin(), m_ConfigList.end(), m_SelectedConfig) == m_ConfigList.end())
    {
        m_SelectedConfig.clear();
    }
    if (m_SelectedConfig.empty() && !m_ConfigList.empty())
        m_SelectedConfig = m_ConfigList.front();
}

void ImGuiUI::DrawSidebarRail(
    const ImVec2& railMin,
    const ImVec2& railMax,
    bool anyRunning,
    bool& exitRequested)
{
    (void)exitRequested;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec4 accent = Accent();

    // Vertical pill background matching the Ocho floating icon bar
    draw->AddRectFilled(railMin, railMax, IM_COL32(10, 18, 26, 205), 10.0f);
    draw->AddRect(railMin, railMax, IM_COL32(164, 220, 230, 30), 10.0f);

    const float cx = (railMin.x + railMax.x) * 0.5f;

    // 1. Top emblem / star / Dribtard icon at y + 22
    const ImVec2 topCenter(cx, railMin.y + 22.0f);
    DrawLogoGlow(ImVec2(topCenter.x - 9.0f, topCenter.y - 9.0f),
        ImVec2(topCenter.x + 9.0f, topCenter.y + 9.0f), 4.0f);
    if (m_MacroIcon)
    {
        draw->AddImageRounded(
            m_MacroIcon,
            ImVec2(topCenter.x - 9.0f, topCenter.y - 9.0f),
            ImVec2(topCenter.x + 9.0f, topCenter.y + 9.0f),
            ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
            IM_COL32(255, 255, 255, 255), 3.0f);
    }
    else
    {
        // Draw crisp 5-point star like the Ocho top emblem
        ImVec2 starPts[10];
        for (int i = 0; i < 10; ++i)
        {
            const float r = (i % 2 == 0) ? 7.0f : 3.0f;
            const float a = -IM_PI * 0.5f + static_cast<float>(i) * (IM_PI / 5.0f);
            starPts[i] = ImVec2(topCenter.x + std::cos(a) * r, topCenter.y + std::sin(a) * r);
        }
        draw->AddConvexPolyFilled(starPts, 10, IM_COL32(248, 248, 252, 255));
    }

    // Subtle status dot below top emblem
    const ImU32 statusCol = anyRunning
        ? IM_COL32(240, 52, 62, 255)
        : (m_Attached ? IM_COL32(72, 212, 115, 240) : IM_COL32(130, 132, 144, 180));
    draw->AddCircleFilled(ImVec2(cx, railMin.y + 39.0f), 2.2f, statusCol, 10);

    // Helper for rail buttons
    auto drawRailSlot = [&](const char* id, float centerY, bool active, const char* tip) -> bool
    {
        const ImVec2 slotMin(railMin.x + 5.0f, centerY - 15.0f);
        const ImVec2 slotMax(railMax.x - 5.0f, centerY + 15.0f);
        ImGui::SetCursorScreenPos(slotMin);
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(slotMax.x - slotMin.x, slotMax.y - slotMin.y));
        const bool hovered = ImGui::IsItemHovered();
#ifdef IMGUI_ENABLE_TEST_ENGINE
        if (GImGui && GImGui->TestEngineHookItems)
            ImGuiTestEngineHook_ItemInfo(GImGui, ImGui::GetItemID(), id, ImGui::GetItemStatusFlags());
#endif
        AddTooltip(tip);

        if (active)
        {
            // Glowing crimson rounded square + left accent bar exactly like Ocho
            draw->AddRectFilled(
                slotMin, slotMax,
                ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.24f)),
                6.0f);
            draw->AddRect(
                slotMin, slotMax,
                ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.55f)),
                6.0f);
            draw->AddRectFilled(
                ImVec2(railMin.x, centerY - 9.0f),
                ImVec2(railMin.x + 2.5f, centerY + 9.0f),
                ImGui::GetColorU32(accent),
                1.5f);
        }
        else if (hovered)
        {
            draw->AddRectFilled(slotMin, slotMax, IM_COL32(255, 255, 255, 16), 6.0f);
        }
        return clicked;
    };

    const ImU32 activeIconCol = ImGui::GetColorU32(ImVec4(
        std::min(1.0f, accent.x + 0.12f),
        std::min(1.0f, accent.y + 0.10f),
        std::min(1.0f, accent.z + 0.10f), 1.0f));
    const ImU32 inactiveIconCol = IM_COL32(218, 220, 228, 235);

    // Tab 1: General (Signal bars icon)
    {
        const float y = railMin.y + 68.0f;
        const bool active = (m_ActivePage == Page::General);
        if (drawRailSlot("##rail_general", y, active, "General: Keybinds, Camera Movement, Block & Privacy"))
        {
            m_ActivePage = Page::General;
            m_MenuOpen = true;
        }
        const ImU32 col = active ? activeIconCol : inactiveIconCol;
        for (int b = 0; b < 4; ++b)
        {
            const float bx = cx - 7.5f + static_cast<float>(b) * 4.2f;
            const float bh = 4.0f + static_cast<float>(b) * 2.6f;
            draw->AddRectFilled(
                ImVec2(bx, y + 6.0f - bh),
                ImVec2(bx + 2.6f, y + 6.0f),
                col, 1.0f);
        }
    }

    // Tab 2: Moonwalk Tech (User / Movement icon)
    {
        const float y = railMin.y + 108.0f;
        const bool active = (m_ActivePage == Page::Moonwalk);
        if (drawRailSlot("##rail_moonwalk", y, active, "Moonwalk Tech: Hold keybind & A/D sequence"))
        {
            m_ActivePage = Page::Moonwalk;
            m_MenuOpen = true;
        }
        const ImU32 col = active ? activeIconCol : inactiveIconCol;
        draw->AddCircleFilled(ImVec2(cx, y - 3.5f), 3.2f, col, 12);
        draw->PathArcTo(ImVec2(cx, y + 7.5f), 5.8f, IM_PI * 1.15f, IM_PI * 1.85f, 12);
        draw->PathStroke(col, 0, 2.0f);
    }

    // Tab 3: Config (Sliders icon)
    {
        const float y = railMin.y + 148.0f;
        const bool active = (m_ActivePage == Page::Configs);
        if (drawRailSlot("##rail_configs", y, active, "Config: Save, load and manage named configurations"))
        {
            m_ActivePage = Page::Configs;
            m_MenuOpen = true;
        }
        const ImU32 col = active ? activeIconCol : inactiveIconCol;
        for (int s = -1; s <= 1; ++s)
        {
            const float sx = cx + static_cast<float>(s) * 4.5f;
            draw->AddLine(ImVec2(sx, y - 6.5f), ImVec2(sx, y + 6.5f), col, 1.4f);
            const float ky = y + (s == 0 ? -2.5f : 2.5f);
            draw->AddCircleFilled(ImVec2(sx, ky), 2.0f, col, 8);
        }
    }

    // Tab 4: Settings / Client (Gear icon)
    {
        const float y = railMin.y + 188.0f;
        const bool active = (m_ActivePage == Page::Settings);
        if (drawRailSlot("##rail_settings", y, active, "Settings: Client, Menu mode, Appearance, Performance & Unload"))
        {
            m_ActivePage = Page::Settings;
            m_MenuOpen = true;
        }
        const ImU32 col = active ? activeIconCol : inactiveIconCol;
        draw->AddCircle(ImVec2(cx, y), 4.2f, col, 16, 1.5f);
        draw->AddCircleFilled(ImVec2(cx, y), 1.6f, col, 10);
        for (int i = 0; i < 6; ++i)
        {
            const float a = static_cast<float>(i) * (IM_PI / 3.0f);
            draw->AddLine(
                ImVec2(cx + std::cos(a) * 4.2f, y + std::sin(a) * 4.2f),
                ImVec2(cx + std::cos(a) * 6.2f, y + std::sin(a) * 6.2f),
                col, 1.5f);
        }
    }

    // Bottom utility button: Portable / Classic layout toggle
    {
        const float yMode = railMax.y - 52.0f;
        if (drawRailSlot("##rail_mode_toggle", yMode, m_PortableMode,
            m_PortableMode ? "Switch to Classic 2-column layout" : "Switch to Portable compact layout"))
        {
            m_PortableMode = !m_PortableMode;
        }
        const ImU32 col = m_PortableMode ? activeIconCol : inactiveIconCol;
        draw->AddRect(ImVec2(cx - 6.0f, yMode - 4.5f), ImVec2(cx + 6.0f, yMode + 4.5f), col, 1.5f, 0, 1.4f);
        draw->AddLine(ImVec2(cx - 2.5f, yMode - 4.5f), ImVec2(cx - 2.5f, yMode + 4.5f), col, 1.2f);
    }

}

void ImGuiUI::SetConfigStatus(const std::string& message)
{
    m_ConfigStatus = message;
}

std::string ImGuiUI::ConsumeConfigSaveRequest()
{
    std::string pending = m_PendingConfigSave;
    m_PendingConfigSave.clear();
    return pending;
}

std::string ImGuiUI::ConsumeConfigLoadRequest()
{
    std::string pending = m_PendingConfigLoad;
    m_PendingConfigLoad.clear();
    return pending;
}

std::string ImGuiUI::ConsumeConfigDeleteRequest()
{
    std::string pending = m_PendingConfigDelete;
    m_PendingConfigDelete.clear();
    return pending;
}

void ImGuiUI::SetUpdateBanner(
    bool available,
    const std::string& version,
    const std::string& notes,
    bool downloading,
    bool readyToApply,
    float progress,
    const std::string& errorText)
{
    m_UpdateAvailable = available;
    m_UpdateVersion = version;
    m_UpdateNotes = notes;
    m_UpdateDownloading = downloading;
    m_UpdateReadyToApply = readyToApply;
    m_UpdateProgress = std::clamp(progress, 0.0f, 1.0f);
    m_UpdateErrorText = errorText;
}

bool ImGuiUI::ConsumeUpdateNowRequest()
{
    const bool pending = m_PendingUpdateNow;
    m_PendingUpdateNow = false;
    return pending;
}

void ImGuiUI::DrawToast(const char* presetName, char digit)
{
    const ImGuiIO& io = ImGui::GetIO();
    constexpr float kToastWidth = 260.0f;
    constexpr float kMargin = 16.0f;
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x - kToastWidth - kMargin, kMargin),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kToastWidth, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.95f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoInputs;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 10.0f));
    ImGui::Begin("##curve_toast", nullptr, flags);
    if (m_GlowIntensity > 0.001f)
    {
        const float pulse = 0.74f + 0.26f * std::sin(static_cast<float>(ImGui::GetTime()) * 2.2f);
        const ImVec4 accent = Accent();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 winMin = ImGui::GetWindowPos();
        const ImVec2 winMax(winMin.x + ImGui::GetWindowWidth(), winMin.y + ImGui::GetWindowHeight());
        draw->AddRect(
            winMin, winMax,
            ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, m_GlowIntensity * pulse * 0.55f)),
            10.0f, 0, 1.5f);
    }
    ImGui::TextUnformatted("Curve changed");
    ImGui::TextColored(Accent(), "%s", presetName ? presetName : "");
    ImGui::TextDisabled("Hotkey Mode: pressed %c", digit ? digit : '?');
    ImGui::End();
    ImGui::PopStyleVar();
}

void ImGuiUI::RenderToast(const char* presetName, char digit)
{
    if (!m_Initialized)
        return;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    DrawToast(presetName, digit);

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void ImGuiUI::DrawGeneralPage(
    MacroSettings& settings,
    MoonwalkSettings& moonwalk,
    bool& startRequested,
    bool& stopRequested,
    bool& emergencyStopRequested,
    bool macroRunning,
    bool& captureExcluded,
    bool& captureExclusionChanged,
    bool captureExclusionFailed,
    bool& exitRequested)
{
    (void)startRequested;
    (void)stopRequested;
    (void)emergencyStopRequested;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild("##general", ImVec2(0.0f, 0.0f), false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec4 accent = Accent();
    // Keep card geometry stable while a clip rectangle reveals the panel.
    const float contentWidth = !m_PortableMode || m_ActivePage == Page::Settings
        ? kWindowSize.x : (m_ActivePage == Page::General ? kCompactGeneralWidth : kCompactWindowWidth);
    const float contentHeight = std::min(ImGui::GetWindowHeight(), kWindowSize.y);
    const ImVec2 winPos = ImGui::GetWindowPos();
    const bool anyRunning = macroRunning || m_MoonwalkRunning;

    // =========================================================================
    // FLOATING VERTICAL ICON SIDEBAR RAIL (Exact Ocho left pill bar)
    // =========================================================================
    const float kRailWidth = 38.0f;
    const float kRailGap = 10.0f;
    const float kMainLeft = 10.0f + kRailWidth + kRailGap; // 58.0f
    const ImVec2 railMin(winPos.x + 10.0f, winPos.y + kPanelTop);
    const ImVec2 railMax(railMin.x + kRailWidth, winPos.y + contentHeight - 44.0f);
    DrawSidebarRail(railMin, railMax, anyRunning, exitRequested);

    // Settings remains full width in compact mode so its layout selector is usable.
    const float mainWidth = contentWidth - kMainLeft - 14.0f;
    const ImVec2 mainMin(winPos.x + kMainLeft, winPos.y + kPanelTop);
    const ImVec2 mainMax(mainMin.x + mainWidth, winPos.y + contentHeight - 44.0f);

    /*
        The panel only pops up while the pointer is on the icon rail or already
        inside the open panel. It also stays open while a control in it is
        being dragged, a dropdown is showing, or a keybind is being captured.
    */
    // While open, the whole window counts, so the Portable layout's narrower
    // panel never closes the menu under the pointer.
    // Open, the zone covers the whole window (margins included) so grabbing its
    // transparent edge to drag keeps the menu open. Closed, it is only the rail.
    // ImGui's hover helper clips to the child window and may reject transparent
    // margins. Test the actual pointer against the whole open window instead.
    const ImVec2 zoneMin = m_MenuOpen ? winPos : railMin;
    const float visibleWidth = kRailWindowWidth +
        (contentWidth - kRailWindowWidth) * m_PanelReveal;
    const ImVec2 zoneMax(
        m_MenuOpen ? winPos.x + visibleWidth : railMax.x,
        m_MenuOpen ? winPos.y + contentHeight : railMax.y);
    const ImGuiIO& io = ImGui::GetIO();
    const bool pointerInZone = io.MousePos.x >= zoneMin.x && io.MousePos.x < zoneMax.x &&
        io.MousePos.y >= zoneMin.y && io.MousePos.y < zoneMax.y;
    const bool pointerInTargetPanel = m_MenuOpen && io.MousePos.x >= winPos.x &&
        io.MousePos.x < winPos.x + contentWidth && io.MousePos.y >= winPos.y &&
        io.MousePos.y < winPos.y + contentHeight;
    // Remember where the press began, not where the moving window is now.
    // A drag can carry the original click outside the window in the next frame.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        m_MenuPressStartedInside = m_MenuOpen && pointerInZone &&
            !ImGui::IsAnyItemHovered();
    else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        m_MenuPressStartedInside = false;
    const bool busy = ImGui::IsAnyItemActive() ||
        (m_MenuPressStartedInside && ImGui::IsMouseDown(ImGuiMouseButton_Left)) ||
        ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) ||
        m_CaptureTarget != CaptureTarget::None;
    // Classic keeps the menu open. Compact shows it only while the pointer is
    // on the rail or inside the open panel.
    m_MenuOpen = !m_PortableMode || pointerInZone || pointerInTargetPanel || busy;

    const float dt = std::clamp(io.DeltaTime, 0.0f, 0.1f);
    const float target = m_MenuOpen ? 1.0f : 0.0f;
    m_PanelReveal += (target - m_PanelReveal) * (1.0f - std::exp(-12.0f * dt));
    if (std::fabs(target - m_PanelReveal) < 0.005f)
        m_PanelReveal = target;
    const float reveal = m_PanelReveal * m_PanelReveal * (3.0f - 2.0f * m_PanelReveal);
    const float revealRight = railMax.x + (mainMax.x - railMax.x) * reveal;
    m_BlurRect = ImVec4(railMin.x, railMin.y, revealRight, railMax.y);
    m_BlurActive = true;

    if (m_PanelReveal <= 0.0f)
    {
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        return;
    }

    // Clip the panel as it slides out. During collapse it is non-interactive;
    // the rail remains available to reverse the animation immediately.
    draw->PushClipRect(ImVec2(mainMin.x, winPos.y),
        ImVec2(std::max(mainMin.x, revealRight), winPos.y + contentHeight), true);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * reveal);
    ImGui::BeginDisabled(!m_MenuOpen);

    // =========================================================================
    // MAIN CONTENT CONTAINER BOX (Adjacent to the floating vertical icon rail)
    // =========================================================================
    draw->AddRectFilled(mainMin, mainMax, IM_COL32(8, 15, 23, 170), 10.0f);
    draw->AddRect(mainMin, mainMax, IM_COL32(164, 220, 230, 35), 10.0f);
    // A quiet animated highlight reinforces the glass edge without obscuring content.
    const float shimmer = 0.25f + 0.10f * std::sin(static_cast<float>(ImGui::GetTime()) * 1.4f);
    draw->AddLine(ImVec2(mainMin.x + 14.0f, mainMin.y + 1.0f),
        ImVec2(mainMin.x + std::min(mainWidth - 14.0f, 155.0f), mainMin.y + 1.0f),
        ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, shimmer)), 1.5f);

    if (m_ActivePage == Page::Settings)
    {
        DrawSettings(settings, captureExcluded, captureExclusionChanged, captureExclusionFailed, exitRequested);
        DrawFooter(exitRequested);
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
        draw->PopClipRect();
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        return;
    }

    if (m_ActivePage == Page::Moonwalk)
    {
        DrawMoonwalkPage(moonwalk);
        DrawFooter(exitRequested);
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
        draw->PopClipRect();
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        return;
    }

    if (m_ActivePage == Page::Configs)
    {
        DrawConfigsPage();
        DrawFooter(exitRequested);
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
        draw->PopClipRect();
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        return;
    }

    // In compact mode only the cards scroll; the rail, backdrop and footer stay
    // fixed, and content cannot paint over the footer.
    if (m_PortableMode)
    {
        ImGui::SetCursorPos(ImVec2(kMainLeft, kPanelTop));
        ImGui::BeginChild("##compact_general_scroll", ImVec2(mainWidth, mainMax.y - mainMin.y),
            false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    }

    // =========================================================================
    // GENERAL PAGE: Boxed Ocho-style cards inside the main panel
    // =========================================================================
    const float innerPad = 10.0f;
    const float colTopY = m_PortableMode ? innerPad : kPanelTop + innerPad;
    constexpr float kColGap = 12.0f;
    const float colWidth = m_PortableMode
        ? (ImGui::GetWindowWidth() - innerPad * 2.0f - ImGui::GetStyle().ScrollbarSize)
        : ((mainWidth - innerPad * 2.0f - kColGap) * 0.5f);
    const float leftX = m_PortableMode ? innerPad : kMainLeft + innerPad;
    const float rightX = m_PortableMode ? leftX : (leftX + colWidth + kColGap);

    ImGui::SetCursorPos(ImVec2(leftX, colTopY));
    ImGui::BeginGroup();

    // Card 1: Keybinds
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(m_CardColor.x, m_CardColor.y, m_CardColor.z, kCardOpacity));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
    ImGui::BeginChild("##card_keybinds", ImVec2(colWidth, 190.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    {
        DrawOchoCardHeader("Keybinds", colWidth, accent);

        auto drawKeyRow = [&](const char* title, const char* subtitle, int vk, const char* keyBtnId, const char* changeBtnId, CaptureTarget target, const char* keyTip, const char* changeTip)
        {
            const float rowStartY = ImGui::GetCursorPosY();
            ImGui::TextUnformatted(title);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.50f, 0.51f, 0.56f, 1.0f));
            ImGui::TextUnformatted(subtitle);
            ImGui::PopStyleColor();

            ImGui::SetCursorPos(ImVec2(colWidth - 138.0f, rowStartY + 2.0f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.14f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.24f, 0.27f, 1.0f));
            if (ImGui::Button((HotkeyManager::GetKeyName(vk) + keyBtnId).c_str(), ImVec2(58.0f, 22.0f)))
                m_CaptureTarget = target;
            ImGui::PopStyleColor(2);
            AddTooltip(keyTip);

            ImGui::SameLine(0.0f, 5.0f);
            if (m_CaptureTarget == target)
            {
                ImGui::TextColored(accent, "Press...");
            }
            else
            {
                if (ImGui::Button((std::string("Change") + changeBtnId).c_str(), ImVec2(62.0f, 22.0f)))
                    m_CaptureTarget = target;
                AddTooltip(changeTip);
            }
            ImGui::SetCursorPosY(rowStartY + 38.0f);
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
        };

        drawKeyRow(
            "Menu Keybind", "Show or hide the overlay",
            m_ToggleKey, "##menu_keybind", "##menu_keybind_change", CaptureTarget::ToggleKey,
            "Hide or show the menu. Change assigns a different toggle key; F12 is reserved for emergency stop.",
            "Click, then press the key you want to use for showing and hiding the menu.");
        DrawRowDivider(colWidth - 24.0f);

        drawKeyRow(
            "Trigger Key", "Starts the macro sequence",
            settings.triggerKey, "##trigger", "##trigger_change", CaptureTarget::TriggerKey,
            "Press this key to run the macro sequence.",
            "Click, then press the key that should start the macro.");
        DrawRowDivider(colWidth - 24.0f);

        drawKeyRow(
            "Ability Key", "Tapped during camera curve",
            settings.activationKey, "##activation", "##activation_change", CaptureTarget::ActivationKey,
            "Key the macro taps for the activation step.",
            "Click, then press the key the macro should tap for activation.");
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    // Card 2: Block (Input / Secondary action)
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(m_CardColor.x, m_CardColor.y, m_CardColor.z, kCardOpacity));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
    ImGui::BeginChild("##card_block", ImVec2(colWidth, 122.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    {
        DrawOchoCardHeader("Block", colWidth, accent);

        const float blkY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Block Key");
        ImGui::TextDisabled("Tapped twice after camera returns");

        ImGui::SetCursorPos(ImVec2(colWidth - 138.0f, blkY + 2.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.14f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.24f, 0.27f, 1.0f));
        if (ImGui::Button((HotkeyManager::GetKeyName(settings.secondaryKey) + "##secondary").c_str(), ImVec2(58.0f, 22.0f)))
            m_CaptureTarget = CaptureTarget::SecondaryKey;
        ImGui::PopStyleColor(2);
        AddTooltip("Key tapped twice after the camera returns. Change assigns a different key.");

        ImGui::SameLine(0.0f, 5.0f);
        if (m_CaptureTarget == CaptureTarget::SecondaryKey)
        {
            ImGui::TextColored(accent, "Press...");
        }
        else
        {
            if (ImGui::Button("Change##secondary_change", ImVec2(62.0f, 22.0f)))
                m_CaptureTarget = CaptureTarget::SecondaryKey;
            AddTooltip("Click, then press the key the macro should tap twice after returning the camera.");
        }

        ImGui::SetCursorPosY(blkY + 38.0f);
        DrawRowDivider(colWidth - 24.0f);

        if (settings.customCurveAngle)
        {
            ImGui::SetNextItemWidth(colWidth - 185.0f);
            ImGui::InputDouble("Delay Before Pressing (sec)", &settings.secondaryDelaySeconds, 0.01, 0.10, "%.3f");
            AddTooltip("Wait this long after restoring the camera before tapping the secondary key.");
        }
        else
        {
            ImGui::TextDisabled("Delay Before Pressing: %.3f sec",
                GetEffectiveCurveSettings(settings).secondaryDelaySeconds);
            AddTooltip("Preset delay between camera restoration and the secondary key taps.");
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    // Card 3: Privacy & Detections
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(m_CardColor.x, m_CardColor.y, m_CardColor.z, kCardOpacity));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
    ImGui::BeginChild("##card_privacy", ImVec2(colWidth, 74.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    {
        DrawOchoCardHeader("Privacy & Capture", colWidth, accent);

        const float rowY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Stream Proof");
        if (captureExclusionFailed)
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "Could not apply capture exclusion.");
        else
            ImGui::TextDisabled("Omit overlay from DWM screen captures");

        ImGui::SetCursorPos(ImVec2(colWidth - 54.0f, rowY + 4.0f));
        if (DrawModernToggle(
                "Exclude UI from screen captures",
                &captureExcluded,
                m_ToggleAnim[0],
                "Ask Windows to omit this overlay from supported screen-capture APIs. It may not work with every recorder, capture method, or older Windows version."))
        {
            captureExclusionChanged = true;
        }
        ImGui::SetCursorPosY(rowY + 32.0f);
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::EndGroup();

    // =========================================================================
    // RIGHT COLUMN: Camera Movement (with live animated 2D trajectory preview)
    // =========================================================================
    if (!m_PortableMode)
        ImGui::SetCursorPos(ImVec2(rightX, colTopY));
    else
        ImGui::SetCursorPos(ImVec2(leftX, ImGui::GetCursorPosY() + 8.0f));

    ImGui::BeginGroup();

    const float camCardHeight = settings.customCurveAngle ? 446.0f : 384.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(m_CardColor.x, m_CardColor.y, m_CardColor.z, kCardOpacity));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
    ImGui::BeginChild("##card_camera", ImVec2(colWidth, camCardHeight), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    {
        DrawOchoCardHeader("Camera Movement", colWidth, accent);

        // Row 1: Hotkey Mode toggle
        const float hkY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Hotkey Mode");
        ImGui::TextDisabled(settings.hotkeyMode
            ? "1 Up  |  2 Back  |  3 Left  |  4 Right"
            : "Switch curve presets with digits 1-4");
        ImGui::SetCursorPos(ImVec2(colWidth - 54.0f, hkY + 5.0f));
        DrawModernToggle(
            "Hotkey Mode",
            &settings.hotkeyMode,
            m_ToggleAnim[1],
            "When enabled, pressing 1 switches to Upwards, 2 to Backwards, 3 to Left, and 4 to Right.");
        ImGui::SetCursorPosY(hkY + 38.0f);
        DrawRowDivider(colWidth - 24.0f);

        // Row 2: Curve Type combo
        const auto& selectedPreset = kCurvePresets.at(static_cast<size_t>(settings.curveType));
        ImGui::BeginDisabled(settings.customCurveAngle);
        ImGui::SetNextItemWidth(colWidth - 125.0f);
        if (ImGui::BeginCombo("Curve Type", selectedPreset.name))
        {
            for (size_t i = 0; i < kCurvePresets.size(); ++i)
            {
                const bool selected = static_cast<size_t>(settings.curveType) == i;
                if (ImGui::Selectable(kCurvePresets[i].name, selected))
                    settings.curveType = static_cast<CurveType>(i);
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        AddTooltip("Choose a preset for the camera's horizontal and vertical movement.");
        ImGui::EndDisabled();
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        DrawRowDivider(colWidth - 24.0f);

        // Row 3: Custom Curve Angle toggle
        const float customY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Custom Curve Angle");
        const MacroSettings effective = GetEffectiveCurveSettings(settings);
        ImGui::TextDisabled("Active: %.0f deg H  |  %.0f deg V",
            effective.horizontalDegrees, effective.verticalDegrees);
        ImGui::SetCursorPos(ImVec2(colWidth - 54.0f, customY + 5.0f));
        if (DrawModernToggle(
                "Custom Curve Angle",
                &settings.customCurveAngle,
                m_ToggleAnim[2],
                "Enable custom mode to edit the curve angles and delay. Enabling copies the selected preset values.") &&
            settings.customCurveAngle)
        {
            settings.horizontalDegrees = selectedPreset.horizontalDegrees;
            settings.verticalDegrees = selectedPreset.verticalDegrees;
            settings.secondaryDelaySeconds = selectedPreset.delayBeforePressingSeconds;
        }
        ImGui::SetCursorPosY(customY + 38.0f);
        DrawRowDivider(colWidth - 24.0f);

        if (settings.customCurveAngle)
        {
            ImGui::SetNextItemWidth(colWidth - 180.0f);
            ImGui::InputDouble("Horizontal Rotation (deg)", &settings.horizontalDegrees, 1.0, 10.0, "%.2f");
            AddTooltip("Set the horizontal camera movement in degrees; positive and negative values turn opposite ways.");

            ImGui::SetNextItemWidth(colWidth - 180.0f);
            ImGui::InputDouble("Vertical / Pitch (deg)", &settings.verticalDegrees, 1.0, 10.0, "%.2f");
            AddTooltip("Set the vertical camera movement in degrees; actual movement may be limited by the game.");
        }

        ImGui::SetNextItemWidth(colWidth - 180.0f);
        ImGui::InputDouble("Roblox Sensitivity", &settings.robloxSensitivity, 0.05, 0.25, "%.3f");
        AddTooltip("Your in-game Roblox mouse sensitivity. Mouse input is scaled by 1 / this value so the camera turns the same distance. Values below 0.01 are clamped.");

        ImGui::SetNextItemWidth(colWidth - 180.0f);
        ImGui::InputDouble("Mouse Counts Per Degree", &settings.mouseCountsPerDegree, 0.1, 1.0, "%.2f");
        AddTooltip("Mouse counts sent per degree of camera turn at Roblox sensitivity 1.0. Scaled by 1 / Roblox Sensitivity at run time. Values outside 0.1 to 100 are clamped on load.");

        ImGui::Dummy(ImVec2(0.0f, 3.0f));
        DrawTrajectoryPreviewCard(
            colWidth - 24.0f,
            138.0f,
            m_PreviewHorizAnim,
            m_PreviewVertAnim,
            settings,
            accent);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::EndGroup();

    if (m_PortableMode)
        ImGui::EndChild();
    DrawFooter(exitRequested);
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    draw->PopClipRect();

    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

void ImGuiUI::DrawUnloadButton(bool& exitRequested, float width)
{
    const ImVec4 accent = Accent();
    constexpr const char* kLabel = "Unload";
    constexpr float kHeight = kFooterPillHeight;

    // The glyph and label are centred as one group across the given width.
    const ImVec2 labelSize = ImGui::CalcTextSize(kLabel);
    const float groupLeft = std::max(0.0f, (width - kUnloadGlyphReserve - labelSize.x) * 0.5f);

    const ImVec2 btnMin = ImGui::GetCursorScreenPos();
    const ImVec2 btnMax(btnMin.x + width, btnMin.y + kHeight);
    const float rounding = kHeight * 0.5f;

    const bool pressed = ImGui::InvisibleButton("##unload_button", ImVec2(width, kHeight));
    const bool highlighted = ImGui::IsItemHovered() || ImGui::IsItemActive();
#ifdef IMGUI_ENABLE_TEST_ENGINE
    if (GImGui && GImGui->TestEngineHookItems)
        ImGuiTestEngineHook_ItemInfo(GImGui, ImGui::GetItemID(), kLabel, ImGui::GetItemStatusFlags());
#endif
    AddTooltip("Closes the overlay and exits the application completely.");

    if (pressed)
        exitRequested = true;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(
        btnMin, btnMax,
        highlighted
            ? ImGui::GetColorU32(ImVec4(accent.x * 0.30f, accent.y * 0.30f, accent.z * 0.30f, 0.96f))
            : IM_COL32(21, 20, 23, 232),
        rounding);
    draw->AddRect(
        btnMin, btnMax,
        highlighted
            ? ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.85f))
            : IM_COL32(255, 255, 255, 26),
        rounding);

    // Compact power glyph: a ring with a gap at the top plus its stem.
    const ImVec2 glyphCenter(btnMin.x + groupLeft + 6.0f, btnMin.y + kHeight * 0.5f);
    const ImU32 glyphCol = highlighted
        ? IM_COL32(252, 240, 242, 255)
        : IM_COL32(158, 160, 172, 255);
    draw->PathArcTo(glyphCenter, 5.2f, -IM_PI * 0.25f, IM_PI * 1.25f, 20);
    draw->PathStroke(glyphCol, 0, 1.5f);
    draw->AddLine(
        ImVec2(glyphCenter.x, glyphCenter.y - 7.6f),
        ImVec2(glyphCenter.x, glyphCenter.y - 1.6f),
        glyphCol, 1.5f);

    draw->AddText(
        ImVec2(btnMin.x + groupLeft + kUnloadGlyphReserve, btnMin.y + (kHeight - labelSize.y) * 0.5f),
        highlighted ? IM_COL32(252, 250, 252, 255) : IM_COL32(198, 198, 208, 255),
        kLabel);
}

void ImGuiUI::DrawFooter(bool& exitRequested)
{
    /*
        Footer row under the panel: version label on the left, with the update
        banner after it. The page tabs live on the icon rail and Unload lives on
        the Settings tab.
    */
    const float rowY = ImGui::GetWindowHeight() - kFooterBottomMargin - kFooterPillHeight;

    // Keep the version and update banner legible over the game or desktop.
    const ImVec2 footerMin = ImVec2(ImGui::GetWindowPos().x + kFooterMargin - 5.0f,
        ImGui::GetWindowPos().y + rowY);
    const ImVec2 footerMax = ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - kFooterMargin,
        footerMin.y + kFooterPillHeight);
    ImGui::GetWindowDrawList()->AddRectFilled(footerMin, footerMax, IM_COL32(8, 15, 23, 230), 7.0f);

    // Show APP_VERSION on the far left of the footer.
    const std::string versionLabel = std::string("DRIBTARD  /  CONTROL SYSTEM   v") + APP_VERSION;
    const ImVec2 verSize = ImGui::CalcTextSize(versionLabel.c_str());
    ImGui::SetCursorPos(ImVec2(kFooterMargin, rowY + (kFooterPillHeight - verSize.y) * 0.5f));
    ImGui::TextDisabled("%s", versionLabel.c_str());

    if (m_UpdateAvailable && !m_UpdateVersion.empty())
    {
        const float bannerLeftX = kFooterMargin + verSize.x + 12.0f;
        DrawUpdateBanner(bannerLeftX, ImGui::GetWindowWidth() - kFooterMargin, rowY);
    }

    (void)exitRequested;
}

void ImGuiUI::DrawUpdateBanner(
    float leftX,
    float maxRightX,
    float rowY)
{
    const float availableWidth = maxRightX - leftX;
    if (availableWidth < 180.0f)
        return;

    const ImVec4 accent = Accent();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImGui::SetCursorPos(ImVec2(leftX, rowY));
    const ImVec2 bannerMin = ImGui::GetCursorScreenPos();
    const ImVec2 bannerMax(bannerMin.x + availableWidth, bannerMin.y + kFooterPillHeight);
    const float rounding = kFooterPillHeight * 0.5f;

    draw->AddRectFilled(bannerMin, bannerMax, IM_COL32(15, 25, 32, 238), rounding);
    draw->AddRect(
        bannerMin, bannerMax,
        ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.55f)),
        rounding);

    // Reserve space on the right inside the banner for the "Update now" button
    // (or the progress bar while downloading).
    constexpr float kBtnWidth = 92.0f;
    constexpr float kInnerPad = 10.0f;

    std::string headline = "Version " + m_UpdateVersion + " available";
    if (!m_UpdateErrorText.empty())
    {
        headline += " - " + m_UpdateErrorText;
    }
    else if (!m_UpdateNotes.empty())
    {
        headline += " - " + m_UpdateNotes;
    }

    const float textMaxRight = bannerMax.x - kBtnWidth - kInnerPad * 2.0f;
    const ImVec2 textSize = ImGui::CalcTextSize(headline.c_str());
    const ImVec2 textPos(
        bannerMin.x + kInnerPad,
        bannerMin.y + (kFooterPillHeight - textSize.y) * 0.5f);

    draw->PushClipRect(
        ImVec2(bannerMin.x + kInnerPad, bannerMin.y),
        ImVec2(textMaxRight, bannerMax.y),
        true);
    draw->AddText(textPos, IM_COL32(238, 239, 246, 255), headline.c_str());
    draw->PopClipRect();

    // Hovering the banner text shows full release notes in a tooltip.
    ImGui::SetCursorScreenPos(bannerMin);
    ImGui::InvisibleButton("##update_banner_info", ImVec2(textMaxRight - bannerMin.x, kFooterPillHeight));
    if (!m_UpdateNotes.empty())
    {
        const std::string tip = "Version " + m_UpdateVersion + " available\n" + m_UpdateNotes;
        AddTooltip(tip.c_str());
    }

    const ImVec2 actionPos(bannerMax.x - kBtnWidth - 4.0f, bannerMin.y + 2.0f);
    const ImVec2 actionSize(kBtnWidth, kFooterPillHeight - 4.0f);

    if (m_UpdateDownloading)
    {
        ImGui::SetCursorScreenPos(actionPos);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(accent.x, accent.y, accent.z, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
        ImGui::ProgressBar(m_UpdateProgress, actionSize);
        ImGui::PopStyleColor(2);
    }
    else
    {
        const char* btnLabel = "Update now";
        ImGui::SetCursorScreenPos(actionPos);
        const ImVec2 btnMin = actionPos;
        const ImVec2 btnMax(actionPos.x + actionSize.x, actionPos.y + actionSize.y);

        if (ImGui::InvisibleButton(btnLabel, actionSize))
            m_PendingUpdateNow = true;

        const bool hovered = ImGui::IsItemHovered();
#ifdef IMGUI_ENABLE_TEST_ENGINE
        if (GImGui && GImGui->TestEngineHookItems)
            ImGuiTestEngineHook_ItemInfo(GImGui, ImGui::GetItemID(), btnLabel, ImGui::GetItemStatusFlags());
#endif
        AddTooltip("Stops the macro if running, downloads and verifies the signed update, and restarts into the new version.");

        draw->AddRectFilled(
            btnMin, btnMax,
            hovered
                ? ImGui::GetColorU32(ImVec4(
                    std::min(1.0f, accent.x + 0.10f),
                    std::min(1.0f, accent.y + 0.08f),
                    std::min(1.0f, accent.z + 0.08f),
                    0.98f))
                : ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.90f)),
            actionSize.y * 0.5f);

        const ImVec2 lblSize = ImGui::CalcTextSize(btnLabel);
        draw->AddText(
            ImVec2(
                btnMin.x + (actionSize.x - lblSize.x) * 0.5f,
                btnMin.y + (actionSize.y - lblSize.y) * 0.5f),
            IM_COL32(252, 250, 252, 255),
            btnLabel);
    }
}

void ImGuiUI::DrawConfigsPage()
{
    const ImVec4 accent = Accent();
    const float contentWidth = ImGui::GetWindowWidth();
    constexpr float kMainLeft = 58.0f;
    constexpr float innerPad = 10.0f;
    constexpr float kColGap = 12.0f;
    const float mainWidth = m_PortableMode
        ? std::min(kCompactWindowWidth - kMainLeft - 14.0f, contentWidth - kMainLeft - 14.0f)
        : (contentWidth - kMainLeft - 14.0f);
    const float colWidth = m_PortableMode
        ? (mainWidth - innerPad * 2.0f)
        : ((mainWidth - innerPad * 2.0f - kColGap) * 0.5f);
    const float leftX = kMainLeft + innerPad;
    const float rightX = m_PortableMode ? leftX : (leftX + colWidth + kColGap);
    constexpr float colTopY = kPanelTop + 10.0f;

    // =========================================================================
    // LEFT COLUMN: Config list card (exact Ocho "Config" card)
    // =========================================================================
    ImGui::SetCursorPos(ImVec2(leftX, colTopY));
    ImGui::BeginGroup();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(m_CardColor.x, m_CardColor.y, m_CardColor.z, kCardOpacity));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
    ImGui::BeginChild("##card_config_list_box", ImVec2(colWidth, 232.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    {
        DrawOchoCardHeader("Config", colWidth, accent);

        if (m_ConfigList.empty())
        {
            ImGui::Dummy(ImVec2(0.0f, 52.0f));
            const char* emptyTitle = "No configs yet";
            const ImVec2 tSz = ImGui::CalcTextSize(emptyTitle);
            ImGui::SetCursorPosX((colWidth - tSz.x) * 0.5f);
            ImGui::TextColored(ImVec4(0.70f, 0.71f, 0.76f, 1.0f), "%s", emptyTitle);

            const char* emptySub = "Type a name and press Save";
            const ImVec2 sSz = ImGui::CalcTextSize(emptySub);
            ImGui::SetCursorPosX((colWidth - sSz.x) * 0.5f);
            ImGui::TextDisabled("%s", emptySub);
        }
        else
        {
            ImGui::BeginChild("##config_items_scroll", ImVec2(colWidth - 24.0f, 176.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
            for (const std::string& name : m_ConfigList)
            {
                const bool isSelected = (m_SelectedConfig == name);
                if (ImGui::Selectable(name.c_str(), isSelected, 0, ImVec2(colWidth - 44.0f, 22.0f)))
                {
                    m_SelectedConfig = name;
                    std::snprintf(m_ConfigNameBuffer, sizeof(m_ConfigNameBuffer), "%s", name.c_str());
                }
            }
            ImGui::EndChild();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::EndGroup();

    // =========================================================================
    // RIGHT COLUMN: Manage card (exact Ocho "Manage" card)
    // =========================================================================
    if (!m_PortableMode)
        ImGui::SetCursorPos(ImVec2(rightX, colTopY));
    else
        ImGui::SetCursorPos(ImVec2(leftX, ImGui::GetCursorPosY() + 8.0f));

    ImGui::BeginGroup();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(m_CardColor.x, m_CardColor.y, m_CardColor.z, kCardOpacity));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
    ImGui::BeginChild("##card_config_manage", ImVec2(colWidth, 232.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    {
        DrawOchoCardHeader("Manage", colWidth, accent);

        const float nameRowY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Name");
        ImGui::SetCursorPos(ImVec2(colWidth - 196.0f, nameRowY - 3.0f));
        ImGui::SetNextItemWidth(184.0f);
        ImGui::InputTextWithHint("##config_name", "default", m_ConfigNameBuffer, sizeof(m_ConfigNameBuffer));
        AddTooltip("Name this setup, then press Save. Letters, numbers, spaces, dashes and underscores.");

        ImGui::SetCursorPosY(nameRowY + 28.0f);
        const float halfBtnW = (colWidth - 24.0f - 8.0f) * 0.5f;

        // Crimson red Save button + Dark Load button side by side
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(accent.x, accent.y, accent.z, 0.92f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(
            std::min(1.0f, accent.x + 0.10f),
            std::min(1.0f, accent.y + 0.08f),
            std::min(1.0f, accent.z + 0.08f), 1.0f));
        if (ImGui::Button("Save", ImVec2(halfBtnW, 26.0f)))
        {
            m_PendingConfigSave = m_ConfigNameBuffer;
            m_ActiveConfigName = m_ConfigNameBuffer;
            m_PendingConfigLoad.clear();
            m_PendingConfigDelete.clear();
        }
        ImGui::PopStyleColor(2);
        AddTooltip("Save every current setting under this name.");

        ImGui::SameLine(0.0f, 8.0f);
        if (ImGui::Button("Load", ImVec2(halfBtnW, 26.0f)))
        {
            const std::string target = !m_SelectedConfig.empty()
                ? m_SelectedConfig
                : std::string(m_ConfigNameBuffer);
            if (!target.empty())
            {
                m_PendingConfigLoad = target;
                m_ActiveConfigName = target;
                m_PendingConfigSave.clear();
                m_PendingConfigDelete.clear();
            }
        }
        AddTooltip("Load the selected or named configuration.");

        // Second button row: Delete + Open folder
        if (ImGui::Button("Delete", ImVec2(halfBtnW, 26.0f)))
        {
            const std::string target = !m_SelectedConfig.empty()
                ? m_SelectedConfig
                : std::string(m_ConfigNameBuffer);
            if (!target.empty())
            {
                m_PendingConfigDelete = target;
                if (m_ActiveConfigName == target)
                    m_ActiveConfigName = "none";
                m_PendingConfigSave.clear();
                m_PendingConfigLoad.clear();
            }
        }
        AddTooltip("Delete the selected or named configuration.");

        ImGui::SameLine(0.0f, 8.0f);
        if (ImGui::Button("Open folder", ImVec2(halfBtnW, 26.0f)))
        {
            char appData[MAX_PATH]{};
            std::string dir = ".";
            if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, appData)))
                dir = std::string(appData) + "\\dribtard\\configs";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            ShellExecuteA(nullptr, "open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        AddTooltip("Open the folder where config files are stored.");

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        DrawRowDivider(colWidth - 24.0f);

        // Status rows: Active & Saved configs count
        const float activeY = ImGui::GetCursorPosY();
        ImGui::TextDisabled("Active");
        const ImVec2 actSz = ImGui::CalcTextSize(m_ActiveConfigName.c_str());
        ImGui::SetCursorPos(ImVec2(colWidth - 12.0f - actSz.x, activeY));
        ImGui::TextUnformatted(m_ActiveConfigName.c_str());

        const float countY = ImGui::GetCursorPosY();
        ImGui::TextDisabled("Saved configs");
        char countBuf[16];
        std::snprintf(countBuf, sizeof(countBuf), "%zu", m_ConfigList.size());
        const ImVec2 cntSz = ImGui::CalcTextSize(countBuf);
        ImGui::SetCursorPos(ImVec2(colWidth - 12.0f - cntSz.x, countY));
        ImGui::TextUnformatted(countBuf);

        if (!m_ConfigStatus.empty())
            ImGui::TextDisabled("%s", m_ConfigStatus.c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::EndGroup();
}

void ImGuiUI::DrawConfigCard(float colWidth)
{
    const ImVec4 accent = Accent();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(m_CardColor.x, m_CardColor.y, m_CardColor.z, kCardOpacity));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
    ImGui::BeginChild("##card_configs", ImVec2(colWidth, 162.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    {
        DrawOchoCardHeader("Configs", colWidth, accent);

        ImGui::SetNextItemWidth(colWidth - 24.0f);
        ImGui::InputTextWithHint("##config_name_quick", "Config name...", m_ConfigNameBuffer, sizeof(m_ConfigNameBuffer));
        AddTooltip("Name this setup, then press Save config. Letters, numbers, spaces, dashes and underscores.");

        if (ImGui::Button("Save config", ImVec2(colWidth - 24.0f, 22.0f)))
        {
            m_PendingConfigSave = m_ConfigNameBuffer;
            m_ActiveConfigName = m_ConfigNameBuffer;
            m_PendingConfigLoad.clear();
            m_PendingConfigDelete.clear();
        }
        AddTooltip("Save every current setting (keys, curve, appearance, FPS cap) under this name.");

        DrawRowDivider(colWidth - 24.0f);

        if (m_ConfigList.empty())
        {
            ImGui::TextDisabled("No saved configs yet.");
        }
        else
        {
            ImGui::BeginChild("##config_list", ImVec2(colWidth - 24.0f, 48.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
            for (const std::string& name : m_ConfigList)
            {
                ImGui::TextUnformatted(name.c_str());
                ImGui::SameLine();
                const float buttonsWidth = 114.0f;
                ImGui::SetCursorPosX(colWidth - 24.0f - buttonsWidth - 10.0f);
                if (ImGui::SmallButton((std::string("Load##cfg_load_") + name).c_str()))
                {
                    m_PendingConfigLoad = name;
                    m_ActiveConfigName = name;
                    m_PendingConfigSave.clear();
                    m_PendingConfigDelete.clear();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton((std::string("Delete##cfg_delete_") + name).c_str()))
                {
                    m_PendingConfigDelete = name;
                    m_PendingConfigSave.clear();
                    m_PendingConfigLoad.clear();
                }
            }
            ImGui::EndChild();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

void ImGuiUI::DrawMoonwalkPage(MoonwalkSettings& moonwalk)
{
    const ImVec4 accent = Accent();
    const float contentWidth = ImGui::GetWindowWidth();

    constexpr float kMainLeft = 58.0f;
    constexpr float innerPad = 10.0f;
    constexpr float kColGap = 12.0f;
    const float mainWidth = m_PortableMode
        ? std::min(kCompactWindowWidth - kMainLeft - 14.0f, contentWidth - kMainLeft - 14.0f)
        : (contentWidth - kMainLeft - 14.0f);
    const float colWidth = m_PortableMode
        ? (mainWidth - innerPad * 2.0f)
        : ((mainWidth - innerPad * 2.0f - kColGap) * 0.5f);
    const float leftX = kMainLeft + innerPad;
    const float rightX = m_PortableMode ? leftX : (leftX + colWidth + kColGap);
    constexpr float colTopY = kPanelTop + 10.0f;

    // =========================================================================
    // LEFT COLUMN: hold keybind and live state
    // =========================================================================
    ImGui::SetCursorPos(ImVec2(leftX, colTopY));
    ImGui::BeginGroup();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(m_CardColor.x, m_CardColor.y, m_CardColor.z, kCardOpacity));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
    ImGui::BeginChild("##card_moonwalk_hold", ImVec2(colWidth, 164.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    {
        DrawOchoCardHeader("Hold Keybind", colWidth, accent);

        const float rowStartY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Hold Key");
        ImGui::TextDisabled("Runs the moonwalk loop while held");

        ImGui::SetCursorPos(ImVec2(colWidth - 138.0f, rowStartY + 2.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.14f, 0.18f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.24f, 0.27f, 1.0f));
        if (ImGui::Button((HotkeyManager::GetKeyName(moonwalk.holdKey) + "##moonwalk_hold").c_str(), ImVec2(58.0f, 22.0f)))
            m_CaptureTarget = CaptureTarget::MoonwalkHoldKey;
        ImGui::PopStyleColor(2);
        AddTooltip("Key you hold to run the moonwalk loop. Change assigns a different hold key.");

        ImGui::SameLine(0.0f, 5.0f);
        if (m_CaptureTarget == CaptureTarget::MoonwalkHoldKey)
        {
            ImGui::TextColored(accent, "Press...");
        }
        else
        {
            if (ImGui::Button("Change##moonwalk_hold_change", ImVec2(62.0f, 22.0f)))
                m_CaptureTarget = CaptureTarget::MoonwalkHoldKey;
            AddTooltip("Click, then press the key you want to hold for Moonwalk Tech. F12 stays reserved for emergency stop.");
        }

        ImGui::SetCursorPosY(rowStartY + 42.0f);
        DrawRowDivider(colWidth - 24.0f);
        ImGui::TextDisabled("Starts on press, stops on release.");
        ImGui::TextDisabled("A and D are released when the loop stops.");
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(m_CardColor.x, m_CardColor.y, m_CardColor.z, kCardOpacity));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
    ImGui::BeginChild("##card_moonwalk_state", ImVec2(colWidth, 112.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    {
        DrawOchoCardHeader("While Held", colWidth, accent);

        ImGui::TextUnformatted("Status");
        if (m_MoonwalkRunning)
        {
            ImGui::TextColored(accent, "RUNNING - repeating A / D taps");
        }
        else
        {
            ImGui::TextDisabled("Hold %s to run the loop", HotkeyManager::GetKeyName(moonwalk.holdKey).c_str());
        }
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        ImGui::TextDisabled("Only runs while the game is focused and licensed.");
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    ImGui::EndGroup();

    // =========================================================================
    // RIGHT COLUMN: the exact tap sequence, drawn as a timeline
    // =========================================================================
    if (!m_PortableMode)
        ImGui::SetCursorPos(ImVec2(rightX, colTopY));
    else
        ImGui::SetCursorPos(ImVec2(leftX, ImGui::GetCursorPosY() + 8.0f));

    ImGui::BeginGroup();

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(m_CardColor.x, m_CardColor.y, m_CardColor.z, kCardOpacity));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
    ImGui::BeginChild("##card_moonwalk_sequence", ImVec2(colWidth, 304.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    {
        DrawOchoCardHeader("Sequence", colWidth, accent);

        struct Step
        {
            const char* key;
            const char* action;
            bool wait;
        };
        const Step steps[] = {
            { "A", "down", false },
            { "", "55 ms", true },
            { "A", "up", false },
            { "", "5 ms", true },
            { "D", "down", false },
            { "", "55 ms", true },
            { "D", "up", false },
        };

        constexpr float kRowHeight = 26.0f;
        constexpr int kStepCount = static_cast<int>(sizeof(steps) / sizeof(steps[0]));

        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float axisX = start.x + 7.0f;
        ImDrawList* draw = ImGui::GetWindowDrawList();

        draw->AddLine(
            ImVec2(axisX, start.y + kRowHeight * 0.5f),
            ImVec2(axisX, start.y + kRowHeight * (kStepCount - 0.5f)),
            IM_COL32(255, 255, 255, 22), 1.0f);

        for (int i = 0; i < kStepCount; ++i)
        {
            const float rowY = start.y + kRowHeight * i;
            const float centerY = rowY + kRowHeight * 0.5f;

            if (steps[i].wait)
            {
                draw->AddCircle(ImVec2(axisX, centerY), 3.5f, IM_COL32(148, 150, 162, 220), 12, 1.2f);
                draw->AddText(
                    ImVec2(axisX + 16.0f, rowY + (kRowHeight - ImGui::GetTextLineHeight()) * 0.5f),
                    IM_COL32(148, 150, 162, 255), steps[i].action);
            }
            else
            {
                draw->AddCircleFilled(ImVec2(axisX, centerY), 4.0f, ImGui::GetColorU32(accent), 12);

                char line[32];
                std::snprintf(line, sizeof(line), "%s %s", steps[i].key, steps[i].action);
                draw->AddText(
                    ImVec2(axisX + 16.0f, rowY + (kRowHeight - ImGui::GetTextLineHeight()) * 0.5f),
                    IM_COL32(236, 237, 244, 255), line);
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + kRowHeight * kStepCount));
        DrawRowDivider(colWidth - 24.0f);
        ImGui::TextDisabled("Repeats until the hold key is released.");
        ImGui::TextDisabled("A and D never overlap: 55 ms each, 5 ms apart.");
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    ImGui::EndGroup();
}

void ImGuiUI::DrawSettings(
    MacroSettings& settings,
    bool& captureExcluded,
    bool& captureExclusionChanged,
    bool captureExclusionFailed,
    bool& exitRequested)
{
    (void)settings;
    (void)captureExclusionFailed;

    // Settings always uses the two-column layout so the Menu mode selector
    // (which lives in the right column) never leaves the visible panel.
    const ImVec4 accent = Accent();
    const float contentWidth = ImGui::GetWindowWidth();

    constexpr float kMainLeft = 58.0f;
    constexpr float innerPad = 10.0f;
    constexpr float kColGap = 12.0f;
    const float mainWidth = contentWidth - kMainLeft - 14.0f;
    const float colWidth = (mainWidth - innerPad * 2.0f - kColGap) * 0.5f;
    const float leftX = kMainLeft + innerPad;
    const float rightX = leftX + colWidth + kColGap;
    constexpr float colTopY = kPanelTop + 10.0f;

    // =========================================================================
    // LEFT COLUMN: Client + Appearance + Configs (matches Ocho Settings left side)
    // =========================================================================
    ImGui::SetCursorPos(ImVec2(leftX, colTopY));
    ImGui::BeginGroup();

    // Card 1: Client (Menu Keybind, UI color, Notifications, Watermark)
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(m_CardColor.x, m_CardColor.y, m_CardColor.z, kCardOpacity));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
    ImGui::BeginChild("##card_client", ImVec2(colWidth, 146.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    {
        DrawOchoCardHeader("Client", colWidth, accent);

        // Row 1: Menu Keybind
        const float mkY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Menu Keybind");
        ImGui::SetCursorPos(ImVec2(colWidth - 82.0f, mkY - 2.0f));
        if (m_CaptureTarget == CaptureTarget::ToggleKey)
        {
            ImGui::TextColored(accent, "Press...");
        }
        else
        {
            if (ImGui::Button((HotkeyManager::GetKeyName(m_ToggleKey) + "##client_menu_key").c_str(), ImVec2(68.0f, 20.0f)))
                m_CaptureTarget = CaptureTarget::ToggleKey;
            AddTooltip("Click, then press the key to show or hide the menu.");
        }
        ImGui::SetCursorPosY(mkY + 24.0f);

        // Row 2: UI color (Accent color swatch matching Ocho)
        const float colY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("UI color");
        ImGui::SetCursorPos(ImVec2(colWidth - 154.0f, colY - 2.0f));
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::ColorEdit3("Accent color", reinterpret_cast<float*>(&m_AccentColor), ImGuiColorEditFlags_NoInputs))
            ApplyTheme();
        AddTooltip("Main highlight color used for checkboxes, headers, sliders and glows.");
        ImGui::SetCursorPosY(colY + 24.0f);

        // Row 3: Notifications
        const float notifY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Notifications");
        ImGui::SetCursorPos(ImVec2(colWidth - 54.0f, notifY - 2.0f));
        DrawModernToggle("Notifications", &m_ShowNotifications, m_ToggleAnim[3], "Show top-right toast notifications when switching curve presets.");

        // Row 4: Card color (base colour of the dark feature cards)
        ImGui::SetCursorPosY(notifY + 24.0f);
        const float cardY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Card color");
        ImGui::SetCursorPos(ImVec2(colWidth - 154.0f, cardY - 2.0f));
        ImGui::SetNextItemWidth(140.0f);
        ImGui::ColorEdit3("Card color", reinterpret_cast<float*>(&m_CardColor), ImGuiColorEditFlags_NoInputs);
        AddTooltip("Base color of the dark cards behind the menu features.");
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    // Card 2: Appearance (Menu transparency / opacity, glows, Darken background, Restore defaults)
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(m_CardColor.x, m_CardColor.y, m_CardColor.z, kCardOpacity));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
    ImGui::BeginChild("##card_appearance", ImVec2(colWidth, 272.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    {
        DrawOchoCardHeader("Appearance", colWidth, accent);

        ImGui::SetNextItemWidth(colWidth - 155.0f);
        ImGui::SliderInt("Gui Size", &m_GuiSizePercent, 85, 115, "%d%%");
        AddTooltip("Display scale indicator.");

        ImGui::SetNextItemWidth(colWidth - 155.0f);
        ImGui::SliderFloat("Menu opacity", &m_PanelOpacity, 0.30f, 1.00f, "%.2f");
        AddTooltip("How solid the menu panel looks (Menu transparency). Lower values let more of the game show through.");
        m_PanelOpacity = std::clamp(m_PanelOpacity, 0.30f, 1.00f);

        ImGui::SetNextItemWidth(colWidth - 155.0f);
        ImGui::SliderFloat("Icon glow strength", &m_GlowIntensity, 0.0f, 1.5f, "%.2f");
        AddTooltip("Soft halo around the emblem. Set to 0 to switch it off.");

        ImGui::SetNextItemWidth(colWidth - 155.0f);
        ImGui::SliderFloat("Window halo strength", &m_WindowGlowIntensity, 0.0f, 1.5f, "%.2f");
        AddTooltip("Animated accent light along the menu's upper edge. Set to 0 to switch it off.");

        DrawRowDivider(colWidth - 24.0f);

        const float dimY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Darken background");
        ImGui::SetCursorPos(ImVec2(colWidth - 54.0f, dimY - 2.0f));
        DrawModernToggle(
            "Darken background",
            &m_DarkenBackground,
            m_ToggleAnim[5],
            "Darken the game behind the menu while it is open so the UI stands out.");
        ImGui::SetCursorPosY(dimY + 24.0f);

        ImGui::BeginDisabled(!m_DarkenBackground);
        ImGui::SetNextItemWidth(colWidth - 155.0f);
        ImGui::SliderFloat("Dim strength", &m_DimOpacity, 0.05f, 0.85f, "%.2f");
        m_DimOpacity = std::clamp(m_DimOpacity, 0.05f, 0.85f);
        AddTooltip("How strongly the background is darkened while the menu is open.");
        ImGui::EndDisabled();

        if (ImGui::Button("Restore defaults", ImVec2(colWidth - 24.0f, 22.0f)))
            RestoreAppearanceDefaults();
        AddTooltip("Bring the accent color, glows, opacity and background dimming back to their defaults.");
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    ImGui::EndGroup();

    // =========================================================================
    // RIGHT COLUMN: Menu mode + Performance + Miscellaneous (with Unload)
    // =========================================================================
    ImGui::SetCursorPos(ImVec2(rightX, colTopY));

    ImGui::BeginGroup();

    // Card 3: Menu mode (Portable vs Classic visual preview selector matching Ocho)
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(m_CardColor.x, m_CardColor.y, m_CardColor.z, kCardOpacity));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
    ImGui::BeginChild("##card_menu_mode", ImVec2(colWidth, 130.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    {
        DrawOchoCardHeader("Menu mode", colWidth, accent);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float boxGap = 10.0f;
        const float boxW = (colWidth - 24.0f - boxGap) * 0.5f;
        const float boxH = 68.0f;
        const ImVec2 basePos = ImGui::GetCursorScreenPos();

        auto drawModePreview = [&](const char* label, const char* id, float xOffset, bool selected, bool isClassicPreview) -> bool
        {
            const ImVec2 pMin(basePos.x + xOffset, basePos.y + 16.0f);
            const ImVec2 pMax(pMin.x + boxW, pMin.y + boxH);
            const ImVec2 lblSz = ImGui::CalcTextSize(label);
            draw->AddText(
                ImVec2(pMin.x + (boxW - lblSz.x) * 0.5f, basePos.y),
                selected ? ImGui::GetColorU32(accent) : IM_COL32(150, 152, 164, 255),
                label);

            ImGui::SetCursorScreenPos(pMin);
            const bool clicked = ImGui::InvisibleButton(id, ImVec2(boxW, boxH));

            draw->AddRectFilled(
                pMin, pMax,
                selected ? ImGui::GetColorU32(ImVec4(accent.x * 0.18f, accent.y * 0.14f, accent.z * 0.16f, 0.85f)) : IM_COL32(14, 14, 18, 255),
                5.0f);
            draw->AddRect(
                pMin, pMax,
                selected ? ImGui::GetColorU32(accent) : IM_COL32(255, 255, 255, 24),
                5.0f, 0, selected ? 1.5f : 1.0f);

            // Mini sidebar rail inside preview
            const ImVec2 miniRailMin(pMin.x + 12.0f, pMin.y + 12.0f);
            const ImVec2 miniRailMax(miniRailMin.x + 6.0f, pMax.y - 12.0f);
            draw->AddRectFilled(
                miniRailMin, miniRailMax,
                selected ? ImGui::GetColorU32(accent) : IM_COL32(95, 97, 108, 200),
                2.0f);

            // Mini main panel inside preview
            const float miniPanelW = isClassicPreview ? (boxW - 34.0f) : (boxW * 0.42f);
            const ImVec2 miniPanelMin(miniRailMax.x + 4.0f, pMin.y + 12.0f);
            const ImVec2 miniPanelMax(miniPanelMin.x + miniPanelW, pMax.y - 12.0f);
            draw->AddRectFilled(
                miniPanelMin, miniPanelMax,
                selected ? ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.48f)) : IM_COL32(55, 56, 64, 160),
                3.0f);
            draw->AddRect(
                miniPanelMin, miniPanelMax,
                selected ? ImGui::GetColorU32(accent) : IM_COL32(130, 132, 145, 160),
                3.0f);

            return clicked;
        };

        if (drawModePreview("Portable", "##mode_portable", 0.0f, m_PortableMode, false))
            m_PortableMode = true;
        if (drawModePreview("Classic", "##mode_classic", boxW + boxGap, !m_PortableMode, true))
            m_PortableMode = false;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    // Card 4: Performance (Limit Menu FPS & Menu FPS Cap)
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(m_CardColor.x, m_CardColor.y, m_CardColor.z, kCardOpacity));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
    ImGui::BeginChild("##card_performance", ImVec2(colWidth, 114.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    {
        DrawOchoCardHeader("Performance", colWidth, accent);

        const float rowY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Limit Menu FPS");
        if (m_LimitMenuFps)
            ImGui::TextDisabled("Menu redraws at most %d FPS", m_MenuFpsCap);
        else
            ImGui::TextDisabled("Follows monitor refresh (vsync)");

        ImGui::SetCursorPos(ImVec2(colWidth - 54.0f, rowY + 4.0f));
        DrawModernToggle(
            "Limit Menu FPS",
            &m_LimitMenuFps,
            m_ToggleAnim[4],
            "Slow the menu down to a fixed frame rate while it is open, to cut GPU and CPU load.");

        ImGui::SetCursorPosY(rowY + 36.0f);
        DrawRowDivider(colWidth - 24.0f);

        ImGui::BeginDisabled(!m_LimitMenuFps);
        ImGui::SetNextItemWidth(colWidth - 180.0f);
        ImGui::InputInt("Menu FPS Cap", &m_MenuFpsCap, 5, 30);
        m_MenuFpsCap = std::clamp(m_MenuFpsCap, kMinMenuFpsCap, kMaxMenuFpsCap);
        AddTooltip("Maximum menu frame rate while the menu is open, between 10 and 480 FPS.");
        ImGui::EndDisabled();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    // Card 5: Miscellaneous + full-width Unload pill (closes the overlay and exits)
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(m_CardColor.x, m_CardColor.y, m_CardColor.z, kCardOpacity));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 8.0f));
    ImGui::BeginChild("##card_misc", ImVec2(colWidth, 128.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    {
        DrawOchoCardHeader("Miscellaneous", colWidth, accent);

        const float spY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Stream proof");
        ImGui::SetCursorPos(ImVec2(colWidth - 54.0f, spY - 2.0f));
        if (DrawModernToggle(
                "Stream proof##misc_toggle",
                &captureExcluded,
                m_ToggleAnim[0],
                "Ask Windows to omit this overlay from supported screen-capture APIs."))
        {
            captureExclusionChanged = true;
        }
        ImGui::SetCursorPosY(spY + 24.0f);

        const float pkY = ImGui::GetCursorPosY();
        ImGui::TextUnformatted("Panic key");
        const char* panicLabel = "F12 (Reserved)";
        const ImVec2 pkSz = ImGui::CalcTextSize(panicLabel);
        ImGui::SetCursorPos(ImVec2(colWidth - 14.0f - pkSz.x, pkY));
        ImGui::TextDisabled("%s", panicLabel);

        ImGui::SetCursorPosY(pkY + 26.0f);
        DrawUnloadButton(exitRequested, colWidth - 24.0f);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    ImGui::EndGroup();
}

void ImGuiUI::Render(
    MacroSettings& settings,
    MoonwalkSettings& moonwalk,
    bool& startRequested,
    bool& stopRequested,
    bool& emergencyStopRequested,
    bool macroRunning,
    bool& captureExcluded,
    bool& captureExclusionChanged,
    bool captureExclusionFailed,
    bool& exitRequested)
{
    if (!m_Initialized)
        return;

    startRequested = false;
    stopRequested = false;
    emergencyStopRequested = false;
    captureExclusionChanged = false;
    exitRequested = false;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    DrawWindow(settings, moonwalk, startRequested, stopRequested, emergencyStopRequested,
        macroRunning, captureExcluded, captureExclusionChanged, captureExclusionFailed,
        exitRequested);

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void ImGuiUI::DrawWindow(
    MacroSettings& settings,
    MoonwalkSettings& moonwalk,
    bool& startRequested,
    bool& stopRequested,
    bool& emergencyStopRequested,
    bool macroRunning,
    bool& captureExcluded,
    bool& captureExclusionChanged,
    bool captureExclusionFailed,
    bool& exitRequested)
{
    UpdateAnimations();
    const float ease = m_AppearAnim * m_AppearAnim * (3.0f - 2.0f * m_AppearAnim);
    m_BlurActive = false;
    // Closed menu: the window is just the icon rail, with no background.
    const bool compactPage = m_PortableMode && m_ActivePage != Page::Settings;
    const float menuWidth = compactPage
        ? (m_ActivePage == Page::General ? kCompactGeneralWidth : kCompactWindowWidth)
        : kWindowSize.x;
    ImGui::SetNextWindowSize(ImVec2(menuWidth, kWindowSize.y), ImGuiCond_Always);
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (m_PortableMode)
    {
        if (!m_WasPortableMode)
        {
            if (ImGuiWindow* window = ImGui::FindWindowByName("##dribtard_main"))
                m_ClassicPosition = window->Pos;
        }
        ImGui::SetNextWindowPos(ImVec2(0.0f, std::max(0.0f, (display.y - kWindowSize.y) * 0.5f)), ImGuiCond_Always);
    }
    else if (m_WasPortableMode)
        ImGui::SetNextWindowPos(m_ClassicPosition, ImGuiCond_Always);
    else
        ImGui::SetNextWindowPos(ImVec2(18.0f, 18.0f), ImGuiCond_FirstUseEver);
    m_WasPortableMode = m_PortableMode;
    if (m_DarkenBackground && m_MenuOpen)
        DrawDimOverlay(ease);
    // The window itself is clear: the frosted blur behind the menu supplies
    // the glass, and the cards add only a light tint.
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings;
    if (m_PortableMode)
        flags |= ImGuiWindowFlags_NoMove;

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ease);
    // An invisible ImGui border around the transparent parent otherwise paints
    // a dark rectangle behind the halo even though its background is clear.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##dribtard_main", nullptr, flags);
    ImGui::PopStyleVar();

    // The menu can be dragged by its transparent margins, but it must stay on
    // screen. A window dragged off the edge would stretch the frosted blur over
    // everything the pointer crossed.
    {
        const ImVec2 size = ImGui::GetWindowSize();
        const ImVec2 pos = ImGui::GetWindowPos();
        const float clampedX = std::clamp(pos.x, 0.0f, std::max(0.0f, display.x - size.x));
        const float clampedY = std::clamp(pos.y, 0.0f, std::max(0.0f, display.y - size.y));
        if (clampedX != pos.x || clampedY != pos.y)
            ImGui::SetWindowPos(ImVec2(clampedX, clampedY));
    }

    const ImVec2 windowMin = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    if (m_PanelReveal > 0.0f)
    {
        const float reveal = m_PanelReveal * m_PanelReveal * (3.0f - 2.0f * m_PanelReveal);
        DrawWindowGlow(windowMin,
            ImVec2(windowMin.x + kRailWindowWidth + (windowSize.x - kRailWindowWidth) * reveal,
                windowMin.y + windowSize.y), ease * reveal);
    }

    /*
        The content wrapper must stay transparent, otherwise its opaque child
        background would cover the translucent panel background.
    */
    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild("##content", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    DrawGeneralPage(settings, moonwalk, startRequested, stopRequested, emergencyStopRequested,
        macroRunning, captureExcluded, captureExclusionChanged, captureExclusionFailed,
        exitRequested);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::End();
    ImGui::PopStyleVar();
}

void ImGuiUI::RenderLicenseScreen(
    char* keyBuffer,
    std::size_t keyBufferSize,
    bool checking,
    const std::string& statusText,
    const std::string& expiryText,
    bool& activateRequested,
    bool& exitRequested)
{
    if (!m_Initialized)
        return;

    activateRequested = false;
    m_BlurActive = false;
    exitRequested = false;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    UpdateAnimations();

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    constexpr ImVec2 kGateSize(460.0f, 320.0f);
    ImGui::SetNextWindowSize(kGateSize, ImGuiCond_Always);
    ImGui::SetNextWindowPos(
        ImVec2(
            (displaySize.x - kGateSize.x) * 0.5f,
            (displaySize.y - kGateSize.y) * 0.5f),
        ImGuiCond_Always);
    if (m_DarkenBackground)
        DrawDimOverlay(m_AppearAnim * m_AppearAnim * (3.0f - 2.0f * m_AppearAnim));
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("##license_gate", nullptr, flags);
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(8, 15, 23, 220), 12.0f);
        draw->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(164, 220, 230, 45), 12.0f);
    }
    {
        const ImVec2 gatePos = ImGui::GetWindowPos();
        const ImVec2 gateSize = ImGui::GetWindowSize();
        m_BlurRect = ImVec4(gatePos.x, gatePos.y, gatePos.x + gateSize.x, gatePos.y + gateSize.y);
        m_BlurActive = true;
    }

    const ImVec2 windowMin = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    DrawWindowGlow(
        windowMin,
        ImVec2(windowMin.x + windowSize.x, windowMin.y + windowSize.y),
        m_AppearAnim * m_AppearAnim * (3.0f - 2.0f * m_AppearAnim));

    const ImVec4 accent = Accent();

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    {
        const char* title = "DRIBTARD  /  ACCESS";
        const ImVec2 titleSize = ImGui::CalcTextSize(title);
        ImGui::SetCursorPosX((windowSize.x - titleSize.x) * 0.5f);
        ImGui::TextUnformatted(title);
    }
    {
        const char* subtitle = "Activate your license to enter the control system.";
        const ImVec2 subtitleSize = ImGui::CalcTextSize(subtitle);
        ImGui::SetCursorPosX((windowSize.x - subtitleSize.x) * 0.5f);
        ImGui::TextDisabled("%s", subtitle);
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    ImGui::SetNextItemWidth(windowSize.x - 64.0f);
    ImGui::SetCursorPosX(32.0f);
    ImGui::InputText(
        "##license_key",
        keyBuffer,
        keyBufferSize,
        ImGuiInputTextFlags_CharsUppercase);
    AddTooltip("Paste the key from Discord, e.g. XXXX-XXXX-XXXX-XXXX.");

    ImGui::Dummy(ImVec2(0.0f, 6.0f));

    ImGui::SetCursorPosX(32.0f);
    ImGui::BeginDisabled(checking);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(accent.x, accent.y, accent.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(
        std::min(1.0f, accent.x + 0.09f),
        std::min(1.0f, accent.y + 0.08f),
        std::min(1.0f, accent.z + 0.08f),
        1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent);

    if (ImGui::Button(checking ? "Checking..." : "Activate", ImVec2(windowSize.x - 64.0f, 32.0f)))
        activateRequested = true;

    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
#ifdef IMGUI_ENABLE_TEST_ENGINE
    if (GImGui && GImGui->TestEngineHookItems)
        ImGuiTestEngineHook_ItemInfo(GImGui, ImGui::GetItemID(), "Activate", ImGui::GetItemStatusFlags());
#endif

    ImGui::Dummy(ImVec2(0.0f, 6.0f));

    // Status line: checking / valid / invalid + reason / offline.
    {
        const char* line = statusText.empty() ? "Enter your license key." : statusText.c_str();
        const ImVec2 lineSize = ImGui::CalcTextSize(line);
        ImGui::SetCursorPosX((windowSize.x - std::min(lineSize.x, windowSize.x - 64.0f)) * 0.5f);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + windowSize.x - 64.0f);

        if (checking)
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.40f, 1.0f), "%s", line);
        else if (!expiryText.empty() || statusText == "License valid.")
            ImGui::TextColored(ImVec4(0.45f, 0.90f, 0.55f, 1.0f), "%s", line);
        else
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.40f, 1.0f), "%s", line);

        ImGui::PopTextWrapPos();
    }

    if (!expiryText.empty())
    {
        const ImVec2 expirySize = ImGui::CalcTextSize(expiryText.c_str());
        ImGui::SetCursorPosX((windowSize.x - expirySize.x) * 0.5f);
        ImGui::TextDisabled("%s", expiryText.c_str());
    }

    ImGui::End();

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void ImGuiUI::Shutdown()
{
    if (!m_Initialized)
        return;

    if (m_MacroIcon)
    {
        m_MacroIcon->Release();
        m_MacroIcon = nullptr;
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CoUninitialize();
    m_Initialized = false;
}
