/***************************************************************************
                          wlabel.cpp  -  description
                             -------------------
    begin                : Wed Jan 5 2005
    copyright            : (C) 2003 by Tue Haste Andersen
    email                : haste@diku.dk
***************************************************************************/

/***************************************************************************
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
***************************************************************************/

#include "widget/wlabel.h"

#include <QFont>

#include "widget/wskincolor.h"

WLabel::WLabel(QWidget* pParent)
        : QLabel(pParent),
          WBaseWidget(this),
          m_skinText(),
          m_longText(),
          m_elideMode(Qt::ElideNone) {
}

WLabel::~WLabel() {
}

void WLabel::setup(QDomNode node, const SkinContext& context) {
    // Colors
    QPalette pal = palette(); //we have to copy out the palette to edit it since it's const (probably for threadsafety)
    if (context.hasNode(node, "BgColor")) {
        m_qBgColor.setNamedColor(context.selectString(node, "BgColor"));
        pal.setColor(this->backgroundRole(), WSkinColor::getCorrectColor(m_qBgColor));
        setAutoFillBackground(true);
    }
    m_qFgColor.setNamedColor(context.selectString(node, "FgColor"));
    pal.setColor(this->foregroundRole(), WSkinColor::getCorrectColor(m_qFgColor));
    setPalette(pal);

    // Text
    if (context.hasNode(node, "Text")) {
        m_skinText = context.selectString(node, "Text");
        setText(m_skinText);
    }
    // Font size
    QString strFontSize;
    if (context.hasNodeSelectString(node, "FontSize", &strFontSize)) {
        int fontsize = strFontSize.toInt();
        setFont(QFont("Helvetica", fontsize, QFont::Normal));
    }

    // Alignment
    QString alignement;
    if (context.hasNodeSelectString(node, "Alignment", &alignement)) {
    	alignement = alignement.toLower();
        if (alignement == "right") {
            setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        } else if (alignement == "center") {
            setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
        } else if (alignement == "left") {
            setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        }
    }

    // Adds an ellipsis to turncated text
    QString elide;
    if (context.hasNodeSelectString(node, "Elide", &elide)) {
    	elide = elide.toLower();
        if (elide == "right") {
            m_elideMode = Qt::ElideRight;
        } else if (elide == "middle") {
            m_elideMode = Qt::ElideMiddle;
        } else if (elide == "left") {
            m_elideMode = Qt::ElideLeft;
        } else if (elide == "none") {
            m_elideMode = Qt::ElideNone;
        }
    }
}

QString WLabel::text() const {
    return m_longText;
}

void WLabel::setText(const QString& text) {
    m_longText = text;
    if (m_elideMode != Qt::ElideNone) {
        QFontMetrics metrics(font());
        // Measure the text for label width - 2 for 1 pixel border
        QString elidedText = metrics.elidedText(m_longText, m_elideMode, width() - 2);
        QLabel::setText(elidedText);
    } else {
        QLabel::setText(m_longText);
    }
}

bool WLabel::event(QEvent* pEvent) {
    if (pEvent->type() == QEvent::ToolTip) {
        updateTooltip();
    }
    return QLabel::event(pEvent);
}

void WLabel::resizeEvent(QResizeEvent* event) {
    QLabel::resizeEvent(event);
    setText(m_longText);
}

void WLabel::fillDebugTooltip(QStringList* debug) {
    WBaseWidget::fillDebugTooltip(debug);
    *debug << QString("Text: \"%1\"").arg(text());
}
