#include "networkutils.h"
#include <QNetworkInterface>
#include <QAbstractSocket>

QString getLocalIPAddress()
{
    const auto addrs = getAllLocalIPAddresses();
    return addrs.isEmpty() ? QStringLiteral("127.0.0.1") : addrs.first();
}

QStringList getAllLocalIPAddresses()
{
    QStringList result;
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp)      ||
            !flags.testFlag(QNetworkInterface::IsRunning) ||
             flags.testFlag(QNetworkInterface::IsLoopBack))
            continue;

        for (const QNetworkAddressEntry &entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol)
                result << entry.ip().toString();
        }
    }
    return result;
}
