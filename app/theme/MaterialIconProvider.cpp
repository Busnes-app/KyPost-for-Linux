#include "theme/MaterialIconProvider.h"

#include <QColor>
#include <QFile>
#include <QPainter>
#include <QRegularExpression>
#include <QSvgRenderer>

MaterialIconProvider::MaterialIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage MaterialIconProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    const QSize out = requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(24, 24);
    if (size)
        *size = out;
    QImage image(out, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    static const QRegularExpression pattern(QStringLiteral("^([a-z0-9-]+)/([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"));
    const QRegularExpressionMatch match = pattern.match(id);
    if (!match.hasMatch())
        return image;
    const QString path = QStringLiteral(":/qml/icons/%1.svg").arg(match.captured(1));
    if (!QFile::exists(path))
        return image;

    QSvgRenderer svg(path);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    svg.render(&painter, QRectF(QPointF(0, 0), QSizeF(out)));
    // Keep the glyph's shape and alpha, take the colour from the URL.
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(image.rect(), QColor(QLatin1Char('#') + match.captured(2)));
    return image;
}
