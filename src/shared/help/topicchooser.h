// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QUrl>
#include <QList>
#include <QString>

#include <QDialog>

#include <memory>

namespace Core {
struct HelpLink;
}

namespace Help::Internal {
class TopicChooserSettings;

#ifdef WITH_TESTS
QObject *createTopicChooserTest();
#endif
} // namespace Help::Internal

class TopicChooser : public QDialog
{
    Q_OBJECT

public:
    explicit TopicChooser(QWidget *parent, const QString &keyword,
                          const QList<Core::HelpLink> &links);
    ~TopicChooser() override;

    QUrl link() const;

private:
    const std::unique_ptr<Help::Internal::TopicChooserSettings> d;
};
