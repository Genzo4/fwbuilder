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
#include "platforms.h"
#include "events.h"

#include "ObjectManipulator.h"
#include "ObjectEditor.h"
#include "ObjectTreeViewItem.h"
#include "ObjectTreeView.h"
#include "newGroupDialog.h"
#include "FWObjectClipboard.h"
#include "FindObjectWidget.h"
#include "interfaceProperties.h"
#include "interfacePropertiesObjectFactory.h"
#include "FWCmdChange.h"
#include "FWCmdAddObject.h"
#include "FWBTree.h"
#include "FWWindow.h"
#include "ProjectPanel.h"
#include "ConfirmDeleteObjectDialog.h"

#include "fwbuilder/Cluster.h"
#include "fwbuilder/FWObject.h"
#include "fwbuilder/IPv6.h"
#include "fwbuilder/Library.h"
#include "fwbuilder/Policy.h"
#include "fwbuilder/Resources.h"

#include <QMessageBox>
#include <QTextEdit>
#include <QTime>
#include <QtDebug>

#include <memory>
#include <algorithm>


using namespace std;
using namespace libfwbuilder;



/* 
 * moveObj is a slot called from the context menu
 */
void ObjectManipulator::moveObj(QAction* action)
{
    int libid = action->data().toInt();

    if (getCurrentObjectTree()->getNumSelected()==0) return;

    ObjectTreeView* ot=getCurrentObjectTree();
    ot->freezeSelection(true);
    FWObject *obj;

    FWObject *targetLib   = idxToLibs[libid];

    vector<FWObject*> so = getCurrentObjectTree()->getSimplifiedSelection();
    for (vector<FWObject*>::iterator i=so.begin();  i!=so.end(); ++i)
    {
        obj= *i;

        if (fwbdebug)
        {
            qDebug("ObjectManipulator::moveObj  obj=%p  obj: %s",
                   obj, obj->getName().c_str() );
        }
        if (Library::isA(obj))
        {
/* We can only move library to the root of the tree. This case only
 * happens when user tries to undelete a library.
 */
            moveObject(m_project->db(),obj);
        } else
        {
            if (obj->isChildOf(targetLib)) continue;

            if ( FWBTree().isSystem(obj) ||
                 Interface::isA(obj)    ||
                 Interface::isA(obj->getParent())) continue;

            moveObject(targetLib, obj);
        }

        QCoreApplication::postEvent(
            mw, new dataModifiedEvent(m_project->getFileName(), obj->getId()));
    }
    ot->freezeSelection(false);

}

void ObjectManipulator::copyObj()
{
    if (getCurrentObjectTree()->getNumSelected()==0) return;
    FWObject *obj;
    FWObjectClipboard::obj_clipboard->clear();

    vector<FWObject*> so = getCurrentObjectTree()->getSimplifiedSelection();

    for (vector<FWObject*>::iterator i=so.begin();  i!=so.end(); ++i)
    {
        obj = *i;
        if ( ! FWBTree().isSystem(obj) )
        {
            // while obj is still part of the tree, do some clean up
            // to avoid problems in the future.  Create
            // InterfaceOptions objects for interfaces because we'll
            // need them for various validations during paste
            // operation.
            Interface *intf = Interface::cast(obj);
            if (intf) intf->getOptionsObject();
            FWObjectClipboard::obj_clipboard->add(obj, m_project);
        }
    }
}

void ObjectManipulator::cutObj()
{
    copyObj();
    delObj();   // works with the list getCurrentObjectTree()->getSelectedObjects()
}

void ObjectManipulator::pasteObj()
{
    if (getCurrentObjectTree()->getNumSelected()==0) return;
    FWObject *target_object = getCurrentObjectTree()->getSelectedObjects().front();
    if (target_object==NULL) return;

    vector<std::pair<int,ProjectPanel*> >::iterator i;
    int idx = 0;
    FWObject *last_object = NULL;
    bool need_to_reload = false;
    map<int,int> map_ids;
    if (fwbdebug)
    {
        qDebug() << "**************** pasteObj loop starts";
        qDebug() << "Target object: " << target_object->getPath().c_str();
    }

    // If we copy many objects in the following loop, and some of them
    // are groups that refer other objects in the same batch, then it
    // is possible that an object would be copied by
    // FWObjectDatabase::recursivelyCopySubtree() by the way of a
    // reference from a group, and then the same object is found in
    // the list of objects to be copied AGAIN. Since this object is
    // already present in the target object tree by the time it needs
    // to be copied again, actuallyPasteTo() chooses the path for
    // copying of objects inside the same tree and creates a copy.  To
    // avoid this, prepare a list of objects to be copied before copy
    // operation starts.

    list<FWObject*> copy_objects;

    for (i= FWObjectClipboard::obj_clipboard->begin();
            i!=FWObjectClipboard::obj_clipboard->end(); ++i)
    {
        FWObject *co = FWObjectClipboard::obj_clipboard->getObjectByIdx(idx);
        copy_objects.push_back(co);
        idx++;
    }

    for (list<FWObject*>::iterator i=copy_objects.begin(); i!=copy_objects.end(); ++i)
    {
        FWObject *co = *i;

        if (fwbdebug)
            qDebug("Copy object %s (id=%d, root=%p)",
                   co->getName().c_str(), co->getId(), co->getRoot());
        if (map_ids.count(co->getId()) > 0)
            continue;

        // Check if we have already copied the same object before
        char s[64];
        sprintf(s, ".copy_of_%p", co->getRoot());
        string dedup_attribute = s;

        sprintf(s, "%d", co->getId());
        FWObject *n_obj =
            target_object->getRoot()->findObjectByAttribute(dedup_attribute, s);
        if (n_obj) continue;

        if (target_object->getRoot() != co->getRoot()) need_to_reload = true;

        last_object = actuallyPasteTo(target_object, co, map_ids);
    }
    if (fwbdebug) qDebug("**************** pasteObj loop done");

    // Note that now all notifications are done by FWCmdAddObject or
    // FWCmdChange
    //
    // if (need_to_reload) loadObjects();
    // openObject(last_object);
}

void ObjectManipulator::duplicateObj(QAction *action)
{
    int libid = action->data().toInt();
    if (getCurrentObjectTree()->getNumSelected()==0) return;

    ObjectTreeView* ot=getCurrentObjectTree();
    ot->freezeSelection(true);
    FWObject *obj;
    FWObject *nobj = NULL;
    vector<FWObject*> so = getCurrentObjectTree()->getSimplifiedSelection();
    for (vector<FWObject*>::iterator i=so.begin();  i!=so.end(); ++i)
    {
        obj= *i;
        if ( FWBTree().isSystem(obj) || Interface::isA(obj) ) continue;
        FWObject *cl = idxToLibs[libid];
        nobj = duplicateObject(cl, obj, "", false);
    }
    editObject(nobj);
    ot->freezeSelection(false);
}

/*
 * Note: this slot gets controlwhen user presses "Delete" key in
 * addition to menu items activation
 */
void ObjectManipulator::delObj()
{
    if (fwbdebug)
        qDebug("ObjectManipulator::delObj selected %d objects ",
               getCurrentObjectTree()->getNumSelected());
    
    if (getCurrentObjectTree()->getNumSelected()==0) return;

    FWObject *current_library = getCurrentLib();
    if (current_library->getId() == FWObjectDatabase::STANDARD_LIB_ID ||
        current_library->getId() == FWObjectDatabase::TEMPLATE_LIB_ID)
        return;

    FWObject *obj;
    
    vector<FWObject*> so = getCurrentObjectTree()->getSimplifiedSelection();
    vector<FWObject*> so2;
    
    for (vector<FWObject*>::iterator i=so.begin(); i!=so.end(); ++i)
    {
        bool del_obj_status = m_project->getDeleteMenuState(*i);
        if (fwbdebug)
            qDebug("ObjectManipulator::delObj object: %s del_obj_status=%d",
                   (*i)->getName().c_str(), del_obj_status);
        if (del_obj_status) so2.push_back(*i);
    }
    
    if (so2.size()==0) return;
    
    if (so2.size()>1 || !Library::isA(so2.front()))
    {
        QApplication::setOverrideCursor( QCursor( Qt::WaitCursor) );
        ConfirmDeleteObjectDialog * dlg = new ConfirmDeleteObjectDialog(this);
        dlg->load(so2);
        QApplication::restoreOverrideCursor();
        if(dlg->exec()==QDialog::Rejected ) return;
    }
        
    /* need to work with a copy of the list of selected objects because
     * some of the methods we call below clear list
     * getCurrentObjectTree()->getSelectedObjects()
     */
    
    try
    {
        for (vector<FWObject*>::iterator i=so2.begin();  i!=so2.end(); ++i)
        {
            obj= *i;
   
            if ( ! FWBTree().isSystem(obj) )
            {
                if (Library::isA(obj))
                {
                    list<FWObject*> ll=m_project->db()->getByType(Library::TYPENAME);
                    if (ll.size()==1)  return;
    
                    if (QMessageBox::warning(
                            this,"Firewall Builder",
                            tr(
                                "When you delete a library, all objects that belong to it\n"
                                "disappear from the tree and all groups and rules that reference them.\n"
                                "You won't be able to reverse this operation later.\n"
                                "Do you still want to delete library %1?")
                            .arg(QString::fromUtf8(obj->getName().c_str())),
                            tr("&Yes"), tr("&No"), QString::null,
                            0, 1 )!=0 ) continue;
                }
    
                if (mw->isEditorVisible() && mw->getOpenedEditor()==obj)
                    mw->hideEditor();

                deleteObject(obj, false);
            }
        }
    }
    catch(FWException &ex)
    {
    }
}

void ObjectManipulator::dumpObj()
{
    if (getCurrentObjectTree()->getNumSelected()==0) return;

    FWObject *obj=getCurrentObjectTree()->getSelectedObjects().front();
    if (obj==NULL) return;
    obj->dump(true,false);
}

void ObjectManipulator::compile()
{
    if (getCurrentObjectTree()->getNumSelected()==0) return;

    vector<FWObject*> so = getCurrentObjectTree()->getSimplifiedSelection();

    set<Firewall*> fo;
    filterFirewallsFromSelection(so, fo);

    if (fwbdebug)
        qDebug("ObjectManipulator::compile filtered %d firewalls",
               int(fo.size()));

    m_project->compile(fo);
}

void ObjectManipulator::install()
{
    if (getCurrentObjectTree()->getNumSelected()==0) return;

    vector<FWObject*> so = getCurrentObjectTree()->getSimplifiedSelection();
    set<Firewall*> fo;
    filterFirewallsFromSelection(so,fo);

    m_project->install(fo);
}

void ObjectManipulator::transferfw()
{
    if (getCurrentObjectTree()->getNumSelected()==0) return;

    vector<FWObject*> so = getCurrentObjectTree()->getSimplifiedSelection();
    set<Firewall*> fo;
    filterFirewallsFromSelection(so, fo);

    m_project->transferfw(fo);
}

void ObjectManipulator::find()
{
    if (getCurrentObjectTree()->getNumSelected()==0) return;

    FWObject *obj=getCurrentObjectTree()->getSelectedObjects().front();
    if (obj==NULL) return;
    m_project->setFDObject(obj);
}

void ObjectManipulator::findObject()
{
    if (getCurrentObjectTree()->getNumSelected()==0) return;

    FWObject *obj=getCurrentObjectTree()->getSelectedObjects().front();
    if (obj==NULL) return;
    mw->findObject( obj );
}

void ObjectManipulator::back()
{
//    if (!validateDialog()) return;

    if (!history.empty())
    {
        history.pop();

/* skip objects that have been deleted */
        while ( ! history.empty())
        {
            if (m_project->db()->findInIndex( history.top().id() )!=NULL) break;
            history.pop();
        }

        if (history.empty())
        {
            mw->enableBackAction();
            return;
        }

        openObject( history.top().item(), false );

        if (mw->isEditorVisible())
        {
            ObjectTreeViewItem *otvi=history.top().item();
            switchObjectInEditor(otvi->getFWObject());
        }
    }
}

void ObjectManipulator::findWhereUsedSlot()
{
    if (getCurrentObjectTree()->getNumSelected()==0) return;

    FWObject *obj = getCurrentObjectTree()->getSelectedObjects().front();
    if (obj==NULL) return;
    mw->findWhereUsed(obj, m_project);

}

