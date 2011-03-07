/*

                          Firewall Builder

                 Copyright (C) 2011 NetCitadel, LLC

  Author:  Vadim Kurland     vadim@fwbuilder.org

  This program is free software which we release under the GNU General Public
  License. You may redistribute and/or modify this program under the terms
  of that license as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  To get a copy of the GNU General Public License, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

#ifndef _IMPORTERTHREAD_H_
#define _IMPORTERTHREAD_H_

#include "Importer.h"

#include <QWidget>
#include <QThread>
#include <QStringList>

#include <map>
#include <set>


namespace libfwbuilder
{
    class FWObject;
    class Firewall;
};

class ImporterThread : public QThread
{
    Q_OBJECT;

    libfwbuilder::FWObject *lib;
    Importer *importer;
    QStringList buffer;
    QString firewallName;
    QString platform;
    QWidget *ui;
    libfwbuilder::Firewall *fw;
    bool stopFlag;
    
public:
    ImporterThread(QWidget *ui,
                   libfwbuilder::FWObject *lib,
                   const QStringList &buffer,
                   const QString &platform,
                   const QString &firewallName);
    virtual ~ImporterThread();

    void run();
    void stop();

    libfwbuilder::Firewall* getFirewallObject() { return fw; }
    
signals:
    void finished();
};


#endif