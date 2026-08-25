// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "secretaspect.h"

#include "coreplugintr.h"
#include "credentialquery.h"

#include <qtkeychain/keychain.h>

#include <QtTaskTree/QTaskTree>
#include <QtTaskTree/QParallelTaskTreeRunner>
#include <QtTaskTree/QSingleTaskTreeRunner>

#include <utils/hostosinfo.h>
#include <utils/qtcsettings.h>

using namespace QKeychain;
using namespace QtTaskTree;
using namespace Utils;

namespace Core {

using ReadCallback = std::function<void(Utils::Result<QString>)>;

class SecretAspectPrivate
{
public:
    void callReadCallbacks(const Result<QString> &value)
    {
        for (const auto &callback : readCallbacks)
            callback(value);
        readCallbacks.clear();
    }

    // The keychain service and key default to the settings key split at the
    // last '.', for callers that do not set them explicitly.
    bool applyTo(CredentialQuery &op, const Key &settingsKey) const
    {
        if (!service.isEmpty() && !key.isEmpty()) {
            op.setService(service);
            op.setKey(key);
            return true;
        }
        QStringList keyParts = stringFromKey(settingsKey).split('.');
        if (keyParts.size() < 2)
            return false;
        op.setKey(keyParts.takeLast());
        op.setService(keyParts.join('.'));
        return true;
    }

    Key plaintextKey(const Key &settingsKey) const
    {
        if (!settingsKey.isEmpty())
            return settingsKey;
        return Utils::keyFromString(service + '.' + key);
    }

public:
    QSingleTaskTreeRunner readRunner;
    QSingleTaskTreeRunner writeRunner;
    bool wasFetchedFromSecretStorage = false;
    bool wasEdited = false;
    bool repeatWriting = false;
    std::vector<ReadCallback> readCallbacks;
    QString value;
    QString service;
    QString key;
    // Why the last fetch failed, shown in the field instead of the secret.
    QString fetchError;
};

SecretAspect::SecretAspect(AspectContainer *container)
    : Utils::BaseAspect(container)
    , d(new SecretAspectPrivate)
{
    // Nothing may be typed in until the secret has been read: what is typed
    // replaces what is stored, and before the read there is nothing to
    // replace it with. requestDisplayText() lifts this.
    setReadOnly(true);
    if (!isSecretStorageAvailable())
        setToolTip(warningThatNoSecretStorageIsAvailable());
}

SecretAspect::~SecretAspect() = default;

void SecretAspect::setService(const QString &service)
{
    d->service = service;
}

void SecretAspect::setKey(const QString &key)
{
    d->key = key;
}

void SecretAspect::readSecret(const std::function<void(Result<QString>)> &cb) const
{
    d->readCallbacks.push_back(cb);

    if (d->readRunner.isRunning())
        return;

    if (!QKeychain::isAvailable()) {
        qWarning() << "No Keychain available, reading from plaintext";
        QtcSettings &settings = Utils::userSettings();
        settings.beginGroup("Secrets");
        QVariant value = settings.value(d->plaintextKey(settingsKey()));
        settings.endGroup();

        d->callReadCallbacks(fromSettingsValue(value).toString());
        return;
    }

    const auto onGetCredentialSetup = [this](CredentialQuery &credential) {
        credential.setOperation(CredentialOperation::Get);
        if (!d->applyTo(credential, settingsKey()))
            return SetupResult::StopWithError;
        return SetupResult::Continue;
    };
    const auto onGetCredentialDone = [this](const CredentialQuery &credential, DoneWith result) {
        if (result == DoneWith::Success) {
            d->value = QString::fromUtf8(credential.data().value_or(QByteArray{}));
            d->wasFetchedFromSecretStorage = true;
            d->callReadCallbacks(d->value);
        } else {
            d->callReadCallbacks(make_unexpected(credential.errorString()));
        }
        return DoneResult::Success;
    };

    d->readRunner.start({CredentialQueryTask(onGetCredentialSetup, onGetCredentialDone)});
}

QString SecretAspect::warningThatNoSecretStorageIsAvailable()
{
    static QString warning
        = Tr::tr("Secret storage is not available! "
                 "Your values will be stored as plaintext in the settings!")
          + (HostOsInfo::isLinuxHost()
                 ? (" " + Tr::tr("You can install libsecret or KWallet to enable secret storage."))
                 : QString());
    return warning;
}

void SecretAspect::readSettings()
{
    readSecret([](const Result<QString> &) {});
}

void SecretAspect::writeSettings() const
{
    if (!d->wasEdited)
        return;

    if (!QKeychain::isAvailable()) {
        QtcSettings &settings = Utils::userSettings();
        settings.beginGroup("Secrets");
        settings.setValue(d->plaintextKey(settingsKey()), toSettingsValue(d->value));
        settings.endGroup();
        d->wasEdited = false;
        return;
    }

    d->repeatWriting = true;

    if (d->writeRunner.isRunning())
        return;

    const auto onSetCredentialSetup = [this](CredentialQuery &credential) {
        credential.setOperation(CredentialOperation::Set);
        credential.setData(d->value.toUtf8());

        if (!d->applyTo(credential, settingsKey()))
            return SetupResult::StopWithError;
        return SetupResult::Continue;
    };

    const auto onSetCredentialsDone = [this](const CredentialQuery &, DoneWith result) {
        if (result == DoneWith::Success)
            d->wasEdited = false;
        return DoneResult::Success;
    };

    const UntilIterator iterator([this](int) { return std::exchange(d->repeatWriting, false); });

    // clang-format off
    d->writeRunner.start(
        For (iterator) >> Do {
            CredentialQueryTask(onSetCredentialSetup, onSetCredentialsDone)
        }
    );
    // clang-format on
}

bool SecretAspect::isDirty() const
{
    return d->wasEdited;
}

AspectPresentation SecretAspect::presentation() const
{
    AspectPresentation p = BaseAspect::presentation();
    // Its own control: the value is not kept here, it has to be fetched. See
    // displayText() and requestDisplayText().
    p.control = AspectControls::Secret;
    // Named after the setting, so that a page with more than one secret on it
    // can be told apart by a test.
    p.objectName = stringFromKey(settingsKey()) + ".secret";
    p.placeholderText = d->fetchError;
    // A warning beside the field, not a message of its own: what it warns
    // about is where the value goes, which is the field.
    if (!isSecretStorageAvailable())
        p.infoType = AspectControls::InfoType::Warning;
    return p;
}

void SecretAspect::requestValue(
    const std::function<void(const Utils::Result<QString> &)> &callback) const
{
    if (d->wasEdited)
        callback(d->value);
    else if (d->wasFetchedFromSecretStorage)
        callback(d->value);
    else
        readSecret(callback);
}

void SecretAspect::setValue(const QString &value)
{
    if (d->value == value && d->wasEdited)
        return;
    d->value = value;
    d->wasEdited = true;
    emit changed();
    emit displayTextChanged();
}

QString SecretAspect::displayText() const
{
    // Empty until the secret has been fetched or set. An empty secret is a
    // legitimate answer too, which is why the delegate waits for the signal
    // rather than for a non-empty string.
    return d->value;
}

void SecretAspect::requestDisplayText()
{
    requestValue([this](const Utils::Result<QString> &value) {
        // requestValue() has already cached whatever it found. A secret that
        // could not be read leaves the field read-only with the reason in it,
        // so that typing does not overwrite a secret that is still there.
        const QString error = value ? QString() : value.error();
        if (d->fetchError != error) {
            d->fetchError = error;
            emit placeholderTextChanged(error);
        }
        if (value)
            setReadOnly(false);
        emit displayTextChanged();
    });
}

void SecretAspect::setVolatileVariantValue(const QVariant &value, Announcement howToAnnounce)
{
    Q_UNUSED(howToAnnounce)
    setValue(value.toString());
}

QVariant SecretAspect::volatileVariantValue() const
{
    return d->value;
}

bool SecretAspect::isSecretStorageAvailable()
{
    return QKeychain::isAvailable();
}

void deleteSecret(const QString &service, const QString &key)
{
    // Written by writeSettings() when no keychain was available, possibly by an
    // earlier run, so remove it regardless of what is available now.
    QtcSettings &settings = Utils::userSettings();
    settings.beginGroup("Secrets");
    settings.remove(Utils::keyFromString(service + '.' + key));
    settings.endGroup();

    if (!QKeychain::isAvailable())
        return;

    const auto onDeleteSetup = [service, key](CredentialQuery &credential) {
        credential.setOperation(CredentialOperation::Delete);
        credential.setService(service);
        credential.setKey(key);
    };
    // Outlives the caller, which is typically going away right now.
    static QParallelTaskTreeRunner runner;
    runner.start({CredentialQueryTask(onDeleteSetup)});
}

} // namespace Core
