#pragma once

#include <QString>

// One secret per name, outside QSettings: Windows Credential Manager (generic credential
// "grimPlayer/<name>"), or on Linux a file only the user can read in the data folder.
namespace CredentialStore {

bool write(const QString &name, const QString &secret);
QString read(const QString &name); // empty when there is none
void remove(const QString &name);

} // namespace CredentialStore
