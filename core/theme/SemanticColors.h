#pragma once
#include <QtGlobal>

// Status colours (danger, warning, success) are per palette since ky-ui 0.8.0:
// see ThemePalette. What is left here is the colour arithmetic they share.
namespace SemanticColors
{
    // Android blend(): base moved `fraction` toward tint, channels truncated.
    quint32 blend(quint32 base, quint32 tint, double fraction);
}
