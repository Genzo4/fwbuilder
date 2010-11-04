/*

                          Firewall Builder

                 Copyright (C) 2003 NetCitadel, LLC

  Author:  Vadim Kurland     vadim@fwbuilder.org

  $Id$

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

#include "../../config.h"
#include "global.h"
#include "utils.h"
#include "utils_no_qt.h"
#include "platforms.h"

#include "newFirewallDialog.h"
#include "ObjConflictResolutionDialog.h"
#include "upgradePredicate.h"
#include "FWBSettings.h"
#include "FWBTree.h"
#include "events.h"
#include "UserWorkflow.h"
#include "FWBApplication.h"
#include "QDesktopWidget"

#include "interfaceProperties.h"
#include "interfacePropertiesObjectFactory.h"

#include "fwbuilder/Library.h"
#include "fwbuilder/Firewall.h"
#include "fwbuilder/Resources.h"
#include "fwbuilder/Policy.h"
#include "fwbuilder/BackgroundOp.h"
#include "fwbuilder/IPv4.h"
#include "fwbuilder/IPv6.h"

#include <qlineedit.h>
#include <qtextedit.h>
#include <qcombobox.h>
#include <qpushbutton.h>
#include <qtoolbutton.h>
#include <qradiobutton.h>
#include <qcheckbox.h>
#include <qtreewidget.h>
#include <qtextbrowser.h>
#include <qmessagebox.h>
#include <qtimer.h>
#include <qlistwidget.h>
#include <qapplication.h>
#include <qcursor.h>
#include <qpixmapcache.h>
#include <qfiledialog.h>
#include <qdir.h>
#include <QtDebug>

#include <algorithm>
#include <iostream>

// must be the last for win
#include "fwbuilder/snmp.h"

using namespace libfwbuilder;
using namespace std;



newFirewallDialog::newFirewallDialog(QWidget *parentw, FWObject *_p) :
    QDialog(parentw)
{
    parent = _p;
    db = parent->getRoot();

    m_dialog = new Ui::newFirewallDialog_q;
    m_dialog->setupUi(this);

    possible_inside_interface_labels.push_back("inside");
    possible_inside_interface_labels.push_back("GREEN");
    possible_inside_interface_labels.push_back("green");

    possible_outside_interface_labels.push_back("outside");
    possible_outside_interface_labels.push_back("RED");
    possible_outside_interface_labels.push_back("red");

    possible_dmz_interface_labels.push_back("dmz");
    possible_dmz_interface_labels.push_back("ORANGE");
    possible_dmz_interface_labels.push_back("orange");


    setControlWidgets(this, m_dialog->stackedWidget,
                      m_dialog->nextButton,
                      m_dialog->finishButton,
                      m_dialog->backButton,
                      m_dialog->cancelButton,
                      m_dialog->titleLabel);

    nfw = NULL;
    tmpldb = NULL;
    snmpPollCompleted = false;
    q = NULL;
    unloadTemplatesLib = false;
    getInterfacesBusy = false;

    timer = new QTimer(this);
    connect( timer, SIGNAL(timeout()), this, SLOT(monitor()));
    connect( m_dialog->selectCustomTemplateLib, SIGNAL(pressed()),
             this, SLOT(browseTemplate()));
    connect( m_dialog->useStandard, SIGNAL(toggled(bool)),
             this, SLOT(updateTemplatePanel()));
    connect( m_dialog->useTemplate, SIGNAL(released()),
             this, SLOT(updateTemplatePanel()));

    m_dialog->useStandard->setChecked(true);
    m_dialog->templateFilePath->setText(tempfname.c_str());
    updateTemplatePanel();

    /* fill in platform. Since iptables is the most popular, start with
     * it.
     */
    QString new_fw_platform = st->getNewFirewallPlatform();

    /* if new_fw_platform is empty, the drop-down list will have empty
     * item which will be current. This is so only on the first run of
     * the program because it remembers chosen platform and uses it on
     * subsequent runs.
     */
    setPlatform(m_dialog->platform, new_fw_platform);

    /* fill in host OS  */
    setHostOS(m_dialog->hostOS, readPlatform(m_dialog->platform), "");

    setNextEnabled( 0, false );

    /*for (int i=0; i<pageCount(); ++i)
        setHelpEnabled( i, false );*/
/*
    //m_dialog->iface_list->setItemMargin( 1 );
    m_dialog->iface_list->setAllColumnsShowFocus( true );

    //m_dialog->iface_sl_list->setItemMargin( 1 );
    m_dialog->iface_dyn->setToolTip(wordWrap(tr("Check option 'dynamic address' for the interface that gets its IP address dynamically via DHCP or PPP protocol.") ,80 ));
    m_dialog->iface_unnum->setToolTip(wordWrap(tr("Check option 'Unnumbered interface' for the interface that does not have an IP address. Examples of interfaces of this kind are those used to terminate PPPoE or VPN tunnels.") ,80 ));
*/
    m_dialog->iface_sl_list->setAllColumnsShowFocus( true );
    QTimer::singleShot(0, m_dialog->obj_name, SLOT(setFocus()));

    currentTemplate = NULL;
    this->m_dialog->interfaceEditor1->clear();
    this->m_dialog->interfaceEditor2->clear();
    this->m_dialog->interfaceEditor1->closeTab();
    this->m_dialog->interfaceEditor2->closeTab();//->removeTab(0);

    this->m_dialog->interfaceEditor1->setExplanation(
        tr("Interfaces with the type set to 'Dynamic IP address' get "
           "IP address by means of DHCP or PPP protocol and do not "
           "require an address here. Interfaces with the type set to "
           "'Static IP address' have statically configured IP address "
           "which should be entered on this page. Interface can have "
           "several IPv4 and IPv6 addresses.")
    );

    this->m_dialog->interfaceEditor2->setExplanation(
        tr("Here you can change IP address of the template interface "
           "to match addresses used on your network. "
           "Interface can have several IPv4 and "
           "IPv6 addresses.")
    );

    this->resize(this->width(), this->minimumHeight());
    int maxheight = (int)(app->desktop()->height()*0.9);
    if (this->height() > maxheight)
        this->resize(this->width(), maxheight);

    showPage(0);
}

void newFirewallDialog::browseTemplate()
{
    QString fileName = QFileDialog::getOpenFileName(
        this, tr("FWBuilder template files"), "",
        tr("FWBuilder template files (*.xml *.fwb *.fwl)"));

    if (fileName=="") return ;

//    QDir dir (fileName);
//    if (dir.exists ())
//    {
    m_dialog->templateFilePath->setText(fileName);
//    }
    updateTemplatePanel();
}

void newFirewallDialog::useStandardTemplate()
{
    m_dialog->templateFilePath->setText(tempfname.c_str());
    updateTemplatePanel();
}

void newFirewallDialog::updateTemplatePanel()
{
    if (st->customTemplatesEnabled() &&
        m_dialog->useTemplate->checkState()==Qt::Checked)
    {
        QString fileName = m_dialog->templateFilePath->text();
        bool using_std = m_dialog->useStandard->isChecked();

        m_dialog->templateGroupBox->setVisible(true);
        m_dialog->templateFilePathLabel->setVisible(true);
        m_dialog->templateFilePath->setVisible(true);
        m_dialog->templateLibExplanation->setVisible(true);

        m_dialog->templateFilePath->setEnabled(!using_std);
        m_dialog->selectCustomTemplateLib->setEnabled(!using_std);

        if (using_std)
        {
            m_dialog->templateFilePath->setText(tempfname.c_str());
        }
    }
    else
    {
        m_dialog->templateGroupBox->setVisible(false);
    }
}

newFirewallDialog::~newFirewallDialog()
{
    delete m_dialog;
    if (timer!=NULL) delete timer;
#ifdef HAVE_LIBSNMP
    if (q!=NULL) delete q;
#endif
}

void newFirewallDialog::changed()
{
    int p = currentPage();

    if (fwbdebug)
        qDebug() << "newFirewallDialog::changed() page=" << p
                 << "use_manual=" << m_dialog->use_manual->isChecked()
                 << "use_snmp=" << m_dialog->use_snmp->isChecked();

    if (p==0)
    {
        setNextEnabled(p,
                       !m_dialog->obj_name->text().isEmpty() &&
                       !readPlatform(m_dialog->platform).isEmpty()
        );
        setHostOS(m_dialog->hostOS, readPlatform(m_dialog->platform), "");
        QString host_os = readHostOS(m_dialog->hostOS);
        m_dialog->interfaceEditor1->setHostOS(host_os);
        m_dialog->interfaceEditor2->setHostOS(host_os);
    }

    if (p==1)
    {

        bool use_snmp = false;

#ifdef HAVE_LIBSNMP
        use_snmp = m_dialog->use_snmp->isChecked();
#else
        use_snmp = false;
        m_dialog->use_snmp->setEnabled( use_snmp );
#endif
        m_dialog->snmpIP->setEnabled( use_snmp );
        m_dialog->snmp_community->setEnabled( use_snmp );
        m_dialog->snmpQuery->setEnabled( use_snmp );
        m_dialog->snmpProgress->setEnabled( use_snmp );
        if ( use_snmp ) m_dialog->snmp_community->setFocus();

        if (use_snmp)
        {
            getIPAddressOfFirewallByName();
        }

        use_snmp = m_dialog->use_manual->isChecked() || snmpPollCompleted;
        setNextEnabled( 1, use_snmp );
    }
    if (fwbdebug)
        qDebug() << "newFirewallDialog::changed() done";
}

void newFirewallDialog::getIPAddressOfFirewallByName()
{
    getInterfacesBusy = true;

    m_dialog->snmpIP->setText("");

    QString name = m_dialog->obj_name->text().toLatin1().constData();
    QApplication::setOverrideCursor( QCursor( Qt::WaitCursor) );
    QString addr = getAddrByName(name, AF_INET);
    QApplication::restoreOverrideCursor();

    if (!addr.isEmpty())
        m_dialog->snmpIP->setText(addr);
    else
    {
        QMessageBox::warning(
            this,"Firewall Builder",
            tr("Address of %1 could not be obtained via DNS")
            .arg(m_dialog->obj_name->text()),
            "&Continue", QString::null, QString::null, 0, 1 );
    }

    getInterfacesBusy = false;
}

void newFirewallDialog::monitor()
{
    if (logger==NULL || q==NULL) return;

#ifdef HAVE_LIBSNMP

    if( logger->ready() )
    {
        QString str = logger->getLine().c_str();
        m_dialog->snmpProgress->moveCursor( QTextCursor::End );
        m_dialog->snmpProgress->insertPlainText( str );
        return;
    }

    if (q->isRunning()) return;

    timer->stop();

    map<int, InterfaceData>* intf = q->getInterfaces();
    map<int, InterfaceData>::iterator i;
    this->m_dialog->interfaceEditor1->clear();
    this->m_dialog->interfaceEditor1->removeTab(0);
    for (i=intf->begin(); i!=intf->end(); ++i)
    {
        InterfaceData* idata = &(i->second);

        if ( idata->ostatus )
        {
            idata->guessLabel(readPlatform(m_dialog->platform).toStdString());
            this->m_dialog->interfaceEditor1->addInterfaceFromData(idata);
        }
    }
    if ( this->m_dialog->interfaceEditor1->count() == 0 )
        this->m_dialog->interfaceEditor1->addNewInterface();

    delete q;
    q=NULL;

#endif

    snmpPollCompleted=true;
    setNextEnabled( 1, true );
}

void newFirewallDialog::getInterfacesViaSNMP()
{
#ifdef HAVE_LIBSNMP

// need to protect from reentry because getAddrByName processes events
    if (q!=NULL || getInterfacesBusy) return;

    snmpPollCompleted=false;
    m_dialog->interfaceEditor1->clear();

    string rcomm=m_dialog->snmp_community->text().toLatin1().constData();

    if ( rcomm.empty() )
    {
        QMessageBox::warning(
            this,"Firewall Builder",
            tr("Missing SNMP community string."),
            "&Continue", QString::null, QString::null, 0, 1 );
        return ;
    }

    getInterfacesBusy = true;

    InetAddr addr;
    try
    {
        addr = InetAddr(m_dialog->snmpIP->text().toStdString());
    }
    catch (FWException &ex)
    {
        try
        {
            QApplication::setOverrideCursor( QCursor( Qt::WaitCursor) );
            QString a = getAddrByName(m_dialog->snmpIP->text(), AF_INET);
            QApplication::restoreOverrideCursor();
            addr = InetAddr(a.toAscii().constData());
            getInterfacesBusy = false;
        }
        catch (FWException &ex)
        {
            QMessageBox::warning(
                this,"Firewall Builder",
                tr("Address of %1 could not be obtained via DNS")
                .arg(m_dialog->snmpIP->text()),
                "&Continue", QString::null, QString::null, 0, 1 );
            getInterfacesBusy = false;
            return ;
        }
    }

    logger=NULL;
    m_dialog->snmpProgress->clear();

    if (q!=NULL) delete q;
    q=new SNMP_interface_query();
    q->init(addr.toString(),rcomm,SNMP_DEFAULT_RETRIES,SNMP_DEFAULT_TIMEOUT);

    timer->setSingleShot(false);
    timer->start(0);

    try
    {
        logger = q->start_operation();

    } catch(const FWException &ex)
    {
        //do nothing
    }

    getInterfacesBusy = false;

#endif
}

bool interfaceCompare(libfwbuilder::Interface *first,
                      libfwbuilder::Interface *second)
{
    return first->getName() < second->getName();
}

bool newFirewallDialog::appropriate(const int page) const
{
    int p  = page;

    switch (p)
    {
    case 0:
    case 4:
        return true;

    case 1:
    case 2:
    case 3:
        return (!m_dialog->useTemplate->isChecked());
    }
    return true;
}

void newFirewallDialog::nextClicked()
{
    if ( currentPage() == 4 )
    {
        if (m_dialog->templateList->currentItem() == NULL)
        {
            QMessageBox::warning(
                    this,"Firewall Builder",
                    tr("Please select template"),
                    tr("&Continue"), QString::null,QString::null,
                    0, 1 );
            showPage(4);
            return;
        }
    }
    if ( currentPage() == 2 )
        if ( !this->m_dialog->interfaceEditor1->isValid() )
            return;
    if (nextRelevant( currentPage() ) > -1)
        showPage(nextRelevant( currentPage() ));
}

void newFirewallDialog::backClicked()
{
    if (previousRelevant( currentPage() ) > -1)
        showPage(previousRelevant( currentPage() ));
}

void newFirewallDialog::showPage(const int page)
{
    FakeWizard::showPage(page);

    int p = page;

    if (fwbdebug)
        qDebug() << "newFirewallDialog::showPage  page=" << page;

// p is a page number _after_ it changed
    switch (p)
    {
    case 0:
        // we get here if user hits "Back" on page 4 (where they
        // choose template object)
        if (tmpldb!=NULL)
        {
            m_dialog->templateList->clear();
            delete tmpldb;
            tmpldb = NULL;
        }
        m_dialog->nextButton->setDefault(true);
        m_dialog->obj_name->setFocus();
        break;

    case 1:
    {
// page 1 is where we choose to configure interfaces manually or via snmp
        m_dialog->snmpIP->setText("");
        changed();  // to properly enable/disable widgets
        m_dialog->nextButton->setDefault(true);
        break;
    }

    case 2:
    {
        if (!Resources::getTargetCapabilityBool(
                readPlatform(m_dialog->platform).toLatin1().constData(),
                "security_levels") )
        {

/* if chosen fw platform does not support security levels,
 * this is the last page
 */
            setNextEnabled( 2, false );
            setFinishEnabled( 2, true );
            m_dialog->finishButton->setDefault(true);
        }
        break;
    }

    case 3:
    {
        if (m_dialog->useTemplate->isChecked())
        {
            showPage( 0 );
            return;
        }

        // Edit security levels
        fillInterfaceSLList();

        setNextEnabled( 3, false );
        setFinishEnabled( 3, true );
        m_dialog->finishButton->setDefault(true);
        break;
    }

    case 4:
    {
        // Show firewall templates
        setFinishEnabled( 4, false );
        setNextEnabled( 4, true );
/* load templates if not loaded */
        if (tmpldb==NULL)
        {

            MessageBoxUpgradePredicate upgrade_predicate(this);
            tmpldb = new FWObjectDatabase();
            tmpldb->setReadOnly( false );
            try
            {
                tmpldb->load(
                    m_dialog->templateFilePath->text().toAscii().data(),
                    &upgrade_predicate, librespath);
            }
            catch (FWException &ex)
            {
                QMessageBox::critical(
                    this,"Firewall Builder",
                    tr("Error loading template library:\n%1")
                    .arg(ex.toString().c_str()),
                    tr("&Continue"), QString::null,QString::null,
                    0, 1 );
            }
        }

        list<FWObject*> fl;

        FWObjectTypedChildIterator libiter =
            tmpldb->findByType(Library::TYPENAME);
        for ( ; libiter!=libiter.end(); ++libiter)
            findFirewalls(*libiter, fl, false);

        QString icn = ":/Icons/Firewall/icon-tree";

        m_dialog->templateList->clear();

        int n=0;
        for (list<FWObject*>::iterator m=fl.begin(); m!=fl.end(); m++,n++)
        {
            FWObject *o=*m;

/*
  Ticket #1492 requested the change to only show the user templates
  that match platform and host OS they choose on the first page of the
  wizard.

  Unfortunately this does not really work because most templates have
  platform and host OS set to "unknown". Either we have to maintain
  many almost identical templates to provide enough choices for all
  possible platforms, or we should not filter by platform.

            string platform = readPlatform(m_dialog->platform).toAscii().constData();
            string host_os = readHostOS(m_dialog->hostOS).toAscii().constData();
            if (o->getStr("platform") != platform || o->getStr("host_OS") != host_os)
                continue;
*/

            QPixmap pm;
            if ( ! QPixmapCache::find( icn, pm) )
            {
                pm.load( icn );
                QPixmapCache::insert( icn, pm);
            }

            QListWidgetItem *twi = new QListWidgetItem;
            twi->setIcon( QIcon(pm) );
            twi->setText( QString(o->getName().c_str()) );

            m_dialog->templateList->addItem( twi );
            templates[ m_dialog->templateList->item( m_dialog->templateList->count()-1 ) ]=o;
        }
        m_dialog->templateList->setFocus();
        m_dialog->templateList->setCurrentItem(0);
        m_dialog->finishButton->setDefault(false);
        m_dialog->nextButton->setDefault(true);
        break;
    }

    case 5:
    {
        // Edit interfaces of the template object
        createFirewallFromTemplate();
        setFinishEnabled( 5, true );
/*
        this->m_dialog->interfaceEditor2->setExplanation(
            tr("Here you can change IP address of the template interface "
               "to match addresses used on your network. "
               "Interface can have several IPv4 and "
               "IPv6 addresses.")
        );
*/
        this->m_dialog->interfaceEditor2->clear();
        this->m_dialog->interfaceEditor2->closeTab();//->removeTab(0);
        this->m_dialog->interfaceEditor2->setCornerWidgetsVisible(false);
        QList<Interface*> interfaces;
        FWObjectTypedChildIterator intiter = nfw->findByType(Interface::TYPENAME);
        for ( ; intiter != intiter.end(); ++intiter )
            interfaces.append(Interface::cast(*intiter));
        sort(interfaces.begin(), interfaces.end(), interfaceCompare);
        foreach(Interface* intr, interfaces)
            m_dialog->interfaceEditor2->addInterface(intr);
        m_dialog->finishButton->setDefault(true);
    }
    }
}

void newFirewallDialog::fillInterfaceSLList()
{
    m_dialog->iface_sl_list->clear();
    foreach(EditedInterfaceData iface,
            this->m_dialog->interfaceEditor1->getData().values() +
            this->m_dialog->interfaceEditor1->getNewData())
    {
        InterfaceData idata;

        idata.name  = iface.name.toStdString();
        idata.label = iface.label.toStdString();
        AddressInfo address;
        bool gotIPv4 = false;
        foreach(AddressInfo addr, iface.addresses.values())
        {
            if (addr.ipv4)
            {
                address = addr;
                gotIPv4 = true;
                break;
            }
        }

        InetAddrMask *iam;// = new InetAddrMask();
        if (iface.type == 0 && iface.addresses.size() != 0)
        {
            if (!gotIPv4) address = iface.addresses.values().first();
            if ( address.ipv4 )
                iam = new InetAddrMask(InetAddr(address.address.toStdString()), InetAddr(address.netmask.toStdString()));
            else
            {
                iam = new InetAddrMask(InetAddr(AF_INET6, address.address.toStdString()), InetAddr(AF_INET6, address.netmask.toStdString()));
            }
            idata.addr_mask.push_back(iam);
        }

        if (gotIPv4)
        {
            try
            {
                idata.guessSecurityLevel( readPlatform(m_dialog->platform).toStdString() );
            }
            catch (FWException &ex)
            {
                QMessageBox::warning( this,"Firewall Builder", ex.toString().c_str(),
                                      "&Continue", QString::null, QString::null, 0, 1 );
                showPage( 2 );
                return;
            }
        }
        else idata.securityLevel = 0;


        QStringList qsl;
        qsl << iface.name//idata.name.c_str()
            << iface.label//idata.label.c_str()
            << address.address
            << QString::number(idata.securityLevel);

        new QTreeWidgetItem(m_dialog->iface_sl_list, qsl);
    }
}

/*
 * this slot is connected to currentItemChanged signal of templateList
 * As a side effect, this slot is called when we clear templateList.
 */
void newFirewallDialog::templateSelected(QListWidgetItem *itm)
{
    if (templates.size()==0) return;
    FWObject *o = templates[itm];
    if (o==NULL) return;
    this->m_dialog->interfaceEditor2->setTemplate(o);
    currentTemplate = o;

    Firewall *fw = Firewall::cast(o);

    m_dialog->templateComment->clear();
    QString s = QString("<a name=\"top\">\n") + fw->getComment().c_str();
    m_dialog->templateComment->append( s );
    m_dialog->templateComment->scrollToAnchor("top");

    bool haveOutside = false;
    bool haveInside  = false;
    bool haveDMZ     = false;
    list<FWObject*> ll = fw->getByType(Interface::TYPENAME);
    for (FWObject::iterator i=ll.begin(); i!=ll.end(); i++)
    {
        Interface *intf = Interface::cast( *i );
        if (std::find(possible_outside_interface_labels.begin(),
                      possible_outside_interface_labels.end(),
                      intf->getLabel()) != possible_outside_interface_labels.end())
        {
            haveOutside=true;
            m_dialog->intfOutsideLine->show();
            m_dialog->intfOutsideText->show();
            fillInterfaceData(intf,m_dialog->intfOutsideText);
        }

        if (std::find(possible_inside_interface_labels.begin(),
                      possible_inside_interface_labels.end(),
                      intf->getLabel()) != possible_inside_interface_labels.end())
        {
            haveInside=true;
            m_dialog->intfInsideLine->show();
            m_dialog->intfInsideText->show();
            fillInterfaceData(intf,m_dialog->intfInsideText);
        }

        if (std::find(possible_dmz_interface_labels.begin(),
                      possible_dmz_interface_labels.end(),
                      intf->getLabel()) != possible_dmz_interface_labels.end())
        {
            haveDMZ=true;
            m_dialog->intfDMZLine->show();
            m_dialog->intfDMZText->show();
            fillInterfaceData(intf,m_dialog->intfDMZText);
        }
    }

    if (!haveOutside) { m_dialog->intfOutsideLine->hide(); m_dialog->intfOutsideText->hide(); }
    if (!haveInside)  { m_dialog->intfInsideLine->hide();  m_dialog->intfInsideText->hide();  }
    if (!haveDMZ)     { m_dialog->intfDMZLine->hide();     m_dialog->intfDMZText->hide();     }
}

void newFirewallDialog::fillInterfaceData(Interface *intf, QTextBrowser *qte)
{
    qte->clear();
    QString s;

    s += "<table border='0' cellspacing='0' cellpadding='0'>";

    s += "<tr>";
    s += "<td>";
    s +=  tr("Interface: %1 (%2)")
        .arg(intf->getName().c_str())
        .arg(intf->getLabel().c_str());
    s += "</td>";
    s += "</tr>";

    s += "<tr>";
    s += "<td>";
    if (intf->isDyn()) s +=  tr("Dynamic address");
    else
        if (intf->isUnnumbered()) s +=  tr("Unnumbered interface");
        else
        {
            const InetAddr *addr = intf->getAddressPtr();
            QString addr_str = (addr) ? addr->toString().c_str() : "";
            const InetAddr *netm = intf->getNetmaskPtr();
            QString netm_str = (netm) ? netm->toString().c_str() : "";
            s += QString("%1/%2").arg(addr_str).arg(netm_str);
        }
    s += "</td>";
    s += "</tr>";
    s += "</table>";
    qte->setText(s);
}

bool newFirewallDialog::validateAddressAndMask(const QString &addr,
                                           const QString &netm)
{
    try
    {
        InetAddr(addr.toLatin1().constData());
    }
    catch (FWException &ex)
    {
        QMessageBox::warning(
            this,"Firewall Builder",
            tr("Invalid address '%1/%2'").arg(addr).arg(netm),
            "&Continue", QString::null, QString::null, 0, 1 );
        return false;
    }
    try
    {
        bool ok = false ;
        int ilen = netm.toInt (&ok);
        if (ok)
        {
            if (ilen < 0 || ilen > 32)
            {
                QMessageBox::warning(
                    this,"Firewall Builder",
                    tr("Invalid address '%1/%2'").arg(addr).arg(netm),
                    "&Continue", QString::null, QString::null, 0, 1 );
                return false;
            }
        }
        else
        {
            InetAddr(netm.toLatin1().constData());
        }

    }
    catch (FWException &ex)
    {
        QMessageBox::warning(
            this,"Firewall Builder",
            tr("Invalid address '%1/%2'").arg(addr).arg(netm),
            "&Continue", QString::null, QString::null, 0, 1 );
        return false;
    }
    return true;
}

void newFirewallDialog::adjustSL(QTreeWidgetItem *itm1)
{
// interface 1 is above 2. Adjust their security levels accordingly
    int sl1 = itm1->text(3).toInt();

    int index = itm1->treeWidget()->indexOfTopLevelItem(itm1);

    QTreeWidgetItem *itm2 = itm1->treeWidget()->topLevelItem(index+1);
    QTreeWidgetItem *itm3 = itm1->treeWidget()->topLevelItem(index-1);

    if (itm2==NULL) sl1=100;
    else
    {
        if (itm3==NULL) sl1=0;
        else
        {
            int sl2 = itm2->text(3).toInt();
            int sl3 = itm3->text(3).toInt();
            sl1 = (sl2+sl3)/2;
        }
    }
    itm1->setText( 3 , QString("%1").arg(sl1) );
}

void newFirewallDialog::upInterface()
{
    QTreeWidgetItem *itm1 = m_dialog->iface_sl_list->currentItem();
    if (itm1==NULL) return;
    int index = m_dialog->iface_sl_list->indexOfTopLevelItem(itm1);

    QTreeWidgetItem *itm2 = m_dialog->iface_sl_list->topLevelItem(index-1);
    if (itm2==NULL) return;
    m_dialog->iface_sl_list->takeTopLevelItem(index);
    m_dialog->iface_sl_list->insertTopLevelItem(index-1, itm1);
    adjustSL(itm1);
}

void newFirewallDialog::downInterface()
{

    QTreeWidgetItem *itm1 = m_dialog->iface_sl_list->currentItem();
    if (itm1==NULL) return;
    int index = m_dialog->iface_sl_list->indexOfTopLevelItem(itm1);

    QTreeWidgetItem *itm2 = m_dialog->iface_sl_list->topLevelItem(index+1);
    if (itm2==NULL) return;
    m_dialog->iface_sl_list->takeTopLevelItem(index);
    m_dialog->iface_sl_list->insertTopLevelItem(index+1, itm1);
    adjustSL(itm1);
}

void newFirewallDialog::cancelClicked()
{
    if (nfw)
    {
        parent->remove(nfw, false);
        delete nfw;
        nfw = NULL;
    }

    if (tmpldb)
    {
        delete tmpldb;
        tmpldb = NULL;
    }

    reject();
}

void newFirewallDialog::finishClicked()
{
    // getting focus to close table cell editor
    // see #1594
    m_dialog->finishButton->setFocus(Qt::OtherFocusReason);

    int p = currentPage();

    if (fwbdebug)
        qDebug() << "newFirewallDialog::finishClicked()"
                 << "p=" << p;

    if ((!this->m_dialog->useTemplate->isChecked()) && p == 2)
    {
        if ( !this->m_dialog->interfaceEditor1->isValid() )
            return;
    }

    if ( p == 5 )
        if ( !this->m_dialog->interfaceEditor2->isValid() )
            return;

    string platform = readPlatform(m_dialog->platform).toAscii().constData();
    string host_os = readHostOS(m_dialog->hostOS).toAscii().constData();

    st->setNewFirewallPlatform(platform.c_str());

    if (p==2)  fillInterfaceSLList();

    if (p==4 || p==5)
    {
        // Creating from a template
        if (nfw==NULL) createFirewallFromTemplate();
        else
            changedAddressesInNewFirewall();

    } else
    {
        wfl->registerFlag(UserWorkflow::NEW_FW_NO_TEMPLATE, true);

        // Create from interface list (obtained either manually or via snmp)
        if ( !this->m_dialog->interfaceEditor1->isValid() )
            return;

        FWObject *o;
        o = db->create(Firewall::TYPENAME);

        if (o==NULL)
        {
            QDialog::accept();
            return;
        }

        o->setName( string( m_dialog->obj_name->text().toUtf8().constData() ) );//.toStdString());
        parent->add(o);

        nfw = Firewall::cast(o);

        o->setStr("platform", platform);
        Resources::setDefaultTargetOptions(platform , nfw);

        o->setStr("host_OS", host_os);
        Resources::setDefaultTargetOptions(host_os , nfw);

/* create interfaces */

        foreach(EditedInterfaceData iface, this->m_dialog->interfaceEditor1->getNewData())
        {
            QString name     =  iface.name;
            QString label    =  iface.label;
            bool    dyn      =  iface.type == 1;
            bool    unnum    =  iface.type == 2;
            QString physaddr =  iface.mac;

            QList<QTreeWidgetItem*> ltwi = m_dialog->iface_sl_list->findItems( name , Qt::MatchExactly );
            assert(!ltwi.empty());
            QTreeWidgetItem *itm2 = ltwi[0];
            assert(itm2!=NULL);

            int sl = itm2->text(3).toInt();

            Interface *oi = Interface::cast(db->create(Interface::TYPENAME));
            assert(oi!=NULL);

            nfw->add(oi);

            oi->setName( string(name.toUtf8().constData()) );
            oi->setLabel( string(label.toUtf8().constData()) );
            oi->setComment( string(iface.comment.toUtf8().constData()) );

            oi->setDyn(dyn);
            oi->setUnnumbered(unnum);
            oi->setSecurityLevel(sl);

            //mw->insertObjectInTree(nfw, oi);
            if (fwbdebug)
                qDebug("Adding interface %s: security_level=%d",
                       oi->getName().c_str(), sl);

            std::auto_ptr<interfaceProperties> int_prop(
                interfacePropertiesObjectFactory::getInterfacePropertiesObject(nfw));
            if (int_prop->looksLikeVlanInterface(name))
            {
                QString base_name;
                int vlan_id;
                int_prop->parseVlan(name, &base_name, &vlan_id);

                oi->getOptionsObject()->setStr("type", "8021q");
                oi->getOptionsObject()->setInt("vlan_id", vlan_id);
            }


            if (iface.type == 0)
            {
                foreach(AddressInfo address, iface.addresses)
                {
                    if (address.address == "0.0.0.0") continue;
                    if (address.ipv4)
                    {
                        string addrname = string( QString("%1:%2:ip").arg(QString(m_dialog->obj_name->text())).arg(name).toUtf8().constData() );
                        IPv4 *oa = IPv4::cast(db->create(IPv4::TYPENAME));
                        oi->add(oa);
                        oa->setName(addrname);
                        oa->setAddress( InetAddr(address.address.toLatin1().constData()) );
                        bool ok = false ;
                        int inetmask = address.netmask.toInt(&ok);
                        if (ok)
                        {
                            oa->setNetmask( InetAddr(inetmask) );
                        }
                        else
                        {
                            oa->setNetmask( InetAddr(address.netmask.toLatin1().constData()) );
                        }
                    }
                    else
                    {
                        string addrname = string ( QString("%1:%2:ip6").arg(QString(m_dialog->obj_name->text())).arg(name).toUtf8().constData() );
                        IPv6 *oa = IPv6::cast(db->create(IPv6::TYPENAME));
                        oi->add(oa);
                        oa->setName(addrname);
                        oa->setAddress(InetAddr(AF_INET6, address.address.toLatin1().constData()) );
                        bool ok = false ;
                        int inetmask = address.netmask.toInt(&ok);
                        if (ok)
                        {
                            oa->setNetmask( InetAddr(AF_INET6, inetmask) );
                        }
                        else
                        {
                            oa->setNetmask(InetAddr(AF_INET6, address.netmask.toLatin1().constData()));
                        }
                    }
                }
            }
        }
    }

    if (tmpldb!=NULL)
    {
        delete tmpldb;
        tmpldb = NULL;
    }

    QDialog::accept();
}

