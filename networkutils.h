#pragma once
#include <QString>
#include <QStringList>

// Returns the first non-loopback IPv4 address, or "127.0.0.1".
QString getLocalIPAddress();

// Returns all non-loopback IPv4 addresses.
QStringList getAllLocalIPAddresses();
