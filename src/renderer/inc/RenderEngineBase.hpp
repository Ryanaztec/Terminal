/*++
Copyright (c) Microsoft Corporation

Module Name:
- RenderEngineBase.hpp

Abstract:
- Implements a set of functions with common behavior across all render engines.
  For example, the behavior for setting the title. The title may change many
  times in the course of a single frame, but the RenderEngine should only
  actually perform it's update operation if at the start of a frame, the new
  window title will be different then the last frames, and it should only ever
  update the title once per frame.

Author(s):
- Mike Griese (migrie) 10-July-2018
--*/
#include "IRenderEngine.hpp"

#pragma once
namespace Microsoft::Console::Render
{
    class RenderEngineBase : public IRenderEngine
    {
    public:
        RenderEngineBase();
        virtual ~RenderEngineBase() = default;

        [[nodiscard]]
        HRESULT InvalidateTitle(const std::wstring& proposedTitle) override;

        [[nodiscard]]
        HRESULT UpdateTitle(const std::wstring& newTitle) override;

    protected:
        [[nodiscard]]
        virtual HRESULT _DoUpdateTitle(const std::wstring& newTitle) = 0;

        bool _titleChanged;
        std::wstring _lastFrameTitle;

    };
}