#pragma once

#include <QQuickImageProvider>

// Serves the bundled Material icons (qrc:/qml/icons/<name>.svg) already
// tinted: image://icon/<name>/<rrggbb or aarrggbb>. Kirigami.Icon's isMask
// recolouring was unreliable on these files and left some glyphs black, which
// is invisible on a dark theme. An unknown name or colour yields an empty
// image rather than a fallback.
class MaterialIconProvider : public QQuickImageProvider
{
public:
    MaterialIconProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};
