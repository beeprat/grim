#include "core/CredentialStore.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#else
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#endif

using namespace Qt::StringLiterals;

namespace CredentialStore {

#ifdef Q_OS_WIN

namespace {
std::wstring target(const QString &name)
{
    return (u"grimPlayer/"_s + name).toStdWString();
}
} // namespace

bool write(const QString &name, const QString &secret)
{
    const std::wstring targetName = target(name);
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<wchar_t *>(targetName.c_str());
    credential.CredentialBlobSize = DWORD(secret.size() * sizeof(char16_t));
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<ushort *>(secret.utf16()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE; // this user on this PC, not roamed
    return CredWriteW(&credential, 0);
}

QString read(const QString &name)
{
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(target(name).c_str(), CRED_TYPE_GENERIC, 0, &credential))
        return {};
    const QString secret = QString::fromUtf16(reinterpret_cast<const char16_t *>(credential->CredentialBlob),
                                              qsizetype(credential->CredentialBlobSize / sizeof(char16_t)));
    CredFree(credential);
    return secret;
}

void remove(const QString &name)
{
    CredDeleteW(target(name).c_str(), CRED_TYPE_GENERIC, 0);
}

#else

// shortcut: a 0600 file, not the desktop keyring; use libsecret if Linux users ask for it.
namespace {
QString path(const QString &name)
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + u"/secret-"_s + name;
}
} // namespace

bool write(const QString &name, const QString &secret)
{
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    QSaveFile file(path(name));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    const QByteArray data = secret.toUtf8();
    return file.write(data) == data.size() && file.commit();
}

QString read(const QString &name)
{
    QFile file(path(name));
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

void remove(const QString &name)
{
    QFile::remove(path(name));
}

#endif

} // namespace CredentialStore
