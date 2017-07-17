// Copyright (c) 2011-2013 The Bitcoin Core developers
// Copyright (c) 2013-2016 The Feathercoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "addressbookpage.h"
#include "ui_addressbookpage.h"

#include "addresstablemodel.h"
#include "optionsmodel.h"
#include "bitcoingui.h"
#include "csvmodelwriter.h"
#include "editaddressdialog.h"
#include "guiutil.h"
#include "scicon.h"

#include "signverifymessagedialog.h"

#include "wallet/wallet.h"
#include "walletmodel.h"
#include "init.h"
#include "base58.h"

#include "walletview.h"
#include "createmultisigaddrdialog.h"

#ifdef USE_QRCODE
#include "qrcodedialog.h"
#endif
#ifdef USE_ZXING
#include "snapwidget.h"
#endif
 
 
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QSortFilterProxyModel>

#include "json/json_spirit.h"
#include "json/json_spirit_reader_template.h"
#include "json/json_spirit_writer_template.h"

using namespace json_spirit;
 
 
AddressBookPage::AddressBookPage(Mode mode, Tabs tab, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AddressBookPage),
    model(0),
    optionsModel(0),
    mode(mode),
    tab(tab)
{
    ui->setupUi(this);

#ifdef Q_OS_MAC // Icons on push buttons are very uncommon on Mac
    ui->newAddress->setIcon(QIcon());
    ui->copyAddress->setIcon(QIcon());
    ui->deleteAddress->setIcon(QIcon());
    ui->exportButton->setIcon(QIcon());
#else
    ui->newAddress->setIcon(SingleColorIcon(":/icons/add"));
    ui->copyAddress->setIcon(SingleColorIcon(":/icons/editcopy"));
    ui->deleteAddress->setIcon(SingleColorIcon(":/icons/remove"));
    ui->exportButton->setIcon(SingleColorIcon(":/icons/export"));
#endif


#ifdef USE_QRCODE
//    ui->showQRCode->setVisible(true);
#endif
#ifndef USE_ZXING
//    ui->importQRCodeButton->setVisible(false);
#endif

    switch(mode)
    {
    case ForSelection:
        switch(tab)
        {
        case SendingTab: setWindowTitle(tr("Choose the address to send coins to")); break;
        case ReceivingTab: setWindowTitle(tr("Choose the address to receive coins with")); break;
        }
        connect(ui->tableView, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(accept()));
        ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ui->tableView->setFocus();
        ui->closeButton->setText(tr("C&hoose"));
        ui->exportButton->hide();
        break;
    case ForEditing:
        switch(tab)
        {
        case SendingTab: setWindowTitle(tr("Sending addresses")); break;
        case ReceivingTab: setWindowTitle(tr("Receiving addresses")); break;
        }
        break;
    }
    switch(tab)
    {
    case SendingTab:
        ui->labelExplanation->setText(tr("These are your Feathercoin addresses for sending payments. Always check the amount and the receiving address before sending coins."));
        ui->deleteAddress->setVisible(true);
        ui->signMessage->setVisible(false);
        break;
    case ReceivingTab:
        ui->labelExplanation->setText(tr("These are your Feathercoin addresses for receiving payments. It is recommended to use a new receiving address for each transaction."));
        ui->deleteAddress->setVisible(false);
        ui->signMessage->setVisible(true);
        ui->newMultiSigAddress->setVisible(false);
        break;
    }

    // Context menu actions
    QAction *copyAddressAction = new QAction(tr("&Copy Address"), this);
    QAction *copyLabelAction = new QAction(tr("Copy &Label"), this);
    QAction *editAction = new QAction(tr("&Edit"), this);
    deleteAction = new QAction(ui->deleteAddress->text(), this);
    #ifdef USE_QRCODE
        QAction *showQRCodeAction = new QAction(ui->showQRCode->text(), this);
    #endif
    QAction *sendCoinsAction = new QAction(tr("Send &Coins"), this);
    QAction *signMessageAction = new QAction(ui->signMessage->text(), this);
    QAction *verifyMessageAction = new QAction(ui->verifyMessage->text(), this);
    QAction *copyPubKeyAction = new QAction(tr("Copy &Public Key"), this);
    QAction *copyPriKeyAction = new QAction(tr("Copy Private Key"), this);

    // Build context menu
    contextMenu = new QMenu();
    contextMenu->addAction(copyAddressAction);
    contextMenu->addAction(copyLabelAction);
    contextMenu->addAction(editAction);
    if(tab == SendingTab)
    {
        contextMenu->addAction(deleteAction);
        contextMenu->addAction(verifyMessageAction);
        contextMenu->addAction(sendCoinsAction);
     }
    contextMenu->addSeparator();
#ifdef USE_QRCODE
    contextMenu->addAction(showQRCodeAction);
#endif

    if(tab == ReceivingTab)
    {
    		contextMenu->addAction(signMessageAction);
        contextMenu->addAction(copyPubKeyAction);
        contextMenu->addAction(copyPriKeyAction);
    }
    
    QAction *MultiSigExportAction = new QAction(tr("Export MultiSig Address"), this);
    contextMenu->addAction(MultiSigExportAction);

    // Connect signals for context menu actions
    connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(on_copyAddress_clicked()));
    connect(copyLabelAction, SIGNAL(triggered()), this, SLOT(onCopyLabelAction()));
    connect(editAction, SIGNAL(triggered()), this, SLOT(onEditAction()));
    connect(deleteAction, SIGNAL(triggered()), this, SLOT(on_deleteAddress_clicked()));
#ifdef USE_QRCODE
   // connect(showQRCodeAction, SIGNAL(triggered()), this, SLOT(on_showQRCode_clicked()));
#endif    
    connect(ui->tableView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));
    connect(ui->closeButton, SIGNAL(clicked()), this, SLOT(accept()));
    
    connect(signMessageAction, SIGNAL(triggered()), this, SLOT(on_signMessage_clicked()));
    connect(verifyMessageAction, SIGNAL(triggered()), this, SLOT(on_verifyMessage_clicked()));
    connect(copyPubKeyAction, SIGNAL(triggered()), this, SLOT(on_copyPubKey_clicked()));
    connect(copyPriKeyAction, SIGNAL(triggered()), this, SLOT(on_copyPriKey_clicked()));
    connect(ui->newMultiSigAddress, SIGNAL(clicked()), this, SLOT(createAddress()));
    connect(MultiSigExportAction, SIGNAL(triggered()), this, SLOT(exportAddress()));
    connect(sendCoinsAction, SIGNAL(triggered()), this, SLOT(onSendCoinsAction()));
}

AddressBookPage::~AddressBookPage()
{
    delete ui;
}

void AddressBookPage::setModel(AddressTableModel *model)
{
    this->model = model;
    if(!model)
        return;

    proxyModel = new QSortFilterProxyModel(this);
    proxyModel->setSourceModel(model);
    proxyModel->setDynamicSortFilter(true);
    proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    switch(tab)
    {
    case ReceivingTab:
        // Receive filter
        proxyModel->setFilterRole(AddressTableModel::TypeRole);
        proxyModel->setFilterFixedString(AddressTableModel::Receive);
        break;
    case SendingTab:
        // Send filter
        proxyModel->setFilterRole(AddressTableModel::TypeRole);
        proxyModel->setFilterFixedString(AddressTableModel::Send);
        break;
    }
    ui->tableView->setModel(proxyModel);
    ui->tableView->sortByColumn(0, Qt::AscendingOrder);

    // Set column widths
#if QT_VERSION < 0x050000
    ui->tableView->horizontalHeader()->setResizeMode(AddressTableModel::Label, QHeaderView::Stretch);
    ui->tableView->horizontalHeader()->setResizeMode(AddressTableModel::Address, QHeaderView::ResizeToContents);
#else
    ui->tableView->horizontalHeader()->setSectionResizeMode(AddressTableModel::Label, QHeaderView::Stretch);
    ui->tableView->horizontalHeader()->setSectionResizeMode(AddressTableModel::Address, QHeaderView::ResizeToContents);
#endif
		ui->tableView->horizontalHeader()->resizeSection(0, 150);
		ui->tableView->horizontalHeader()->resizeSection(1, 850);
		ui->tableView->horizontalHeader()->setStretchLastSection(true);
		
    connect(ui->tableView->selectionModel(), SIGNAL(selectionChanged(QItemSelection,QItemSelection)),
        this, SLOT(selectionChanged()));

    // Select row for newly created address
    connect(model, SIGNAL(rowsInserted(QModelIndex,int,int)), this, SLOT(selectNewAddress(QModelIndex,int,int)));

    selectionChanged();
}


void AddressBookPage::setOptionsModel(OptionsModel *optionsModel)
{
    this->optionsModel = optionsModel;
}

void AddressBookPage::on_copyAddress_clicked()
{
    GUIUtil::copyEntryData(ui->tableView, AddressTableModel::Address);
}

void AddressBookPage::onCopyLabelAction()
{
    GUIUtil::copyEntryData(ui->tableView, AddressTableModel::Label);
}

void AddressBookPage::onEditAction()
{
    if(!model)
        return;

    if(!ui->tableView->selectionModel())
        return;
    QModelIndexList indexes = ui->tableView->selectionModel()->selectedRows();
    if(indexes.isEmpty())
        return;

    EditAddressDialog dlg(
        tab == SendingTab ?
        EditAddressDialog::EditSendingAddress :
        EditAddressDialog::EditReceivingAddress, this);
    dlg.setModel(model);
    QModelIndex origIndex = proxyModel->mapToSource(indexes.at(0));
    dlg.loadRow(origIndex.row());
    dlg.exec();
}

void AddressBookPage::on_newAddress_clicked()
{
    if(!model)
        return;

    EditAddressDialog dlg(
        tab == SendingTab ?
        EditAddressDialog::NewSendingAddress :
        EditAddressDialog::NewReceivingAddress, this);
    dlg.setModel(model);
    if(dlg.exec())
    {
        newAddressToSelect = dlg.getAddress();
    }
}

void AddressBookPage::on_deleteAddress_clicked()
{
    QTableView *table = ui->tableView;
    if(!table->selectionModel())
        return;

    QModelIndexList indexes = table->selectionModel()->selectedRows();
    if(!indexes.isEmpty())
    {
        table->model()->removeRow(indexes.at(0).row());
    }
}

void AddressBookPage::selectionChanged()
{
    // Set button states based on selected tab and selection
    QTableView *table = ui->tableView;
    if(!table->selectionModel())
        return;

    if(table->selectionModel()->hasSelection())
    {
        switch(tab)
        {
        case SendingTab:
            // In sending tab, allow deletion of selection
            ui->deleteAddress->setEnabled(true);
            ui->deleteAddress->setVisible(true);
            deleteAction->setEnabled(true);
            break;
        case ReceivingTab:
            // Deleting receiving addresses, however, is not allowed
            ui->deleteAddress->setEnabled(false);
            ui->deleteAddress->setVisible(false);
            deleteAction->setEnabled(false);
            break;
        }
        ui->copyAddress->setEnabled(true);
        #ifdef USE_QRCODE
           ui->showQRCode->setEnabled(true);
        #endif
        #ifdef USE_ZXING
           ui->importQRCodeButton->setEnabled(true);
        #endif
    }
    else
    {
        ui->deleteAddress->setEnabled(false);
        ui->copyAddress->setEnabled(false);
        #ifdef USE_QRCODE
            ui->showQRCode->setEnabled(false);
        #endif
    }
}

void AddressBookPage::done(int retval)
{
    QTableView *table = ui->tableView;
    if(!table->selectionModel() || !table->model())
        return;

    // Figure out which address was selected, and return it
    QModelIndexList indexes = table->selectionModel()->selectedRows(AddressTableModel::Address);

    Q_FOREACH (const QModelIndex& index, indexes) {
        QVariant address = table->model()->data(index);
        returnValue = address.toString();
    }

    if(returnValue.isEmpty())
    {
        // If no address entry selected, return rejected
        retval = Rejected;
    }

    QDialog::done(retval);
}

void AddressBookPage::on_exportButton_clicked()
{
    // CSV is currently the only supported format
    QString filename = GUIUtil::getSaveFileName(this,
        tr("Export Address List"), QString(),
        tr("Comma separated file (*.csv)"), NULL);

    if (filename.isNull())
        return;

    CSVModelWriter writer(filename);

    // name, column, role
    writer.setModel(proxyModel);
    writer.addColumn("Label", AddressTableModel::Label, Qt::EditRole);
    writer.addColumn("Address", AddressTableModel::Address, Qt::EditRole);

    if(!writer.write()) {
        QMessageBox::critical(this, tr("Exporting Failed"),
            tr("There was an error trying to save the address list to %1. Please try again.").arg(filename));
    }
}

void AddressBookPage::contextualMenu(const QPoint &point)
{
    QModelIndex index = ui->tableView->indexAt(point);
    if(index.isValid())
    {
        contextMenu->exec(QCursor::pos());
    }
}

void AddressBookPage::selectNewAddress(const QModelIndex &parent, int begin, int /*end*/)
{
    QModelIndex idx = proxyModel->mapFromSource(model->index(begin, AddressTableModel::Address, parent));
    if(idx.isValid() && (idx.data(Qt::EditRole).toString() == newAddressToSelect))
    {
        // Select row of newly created address, once
        ui->tableView->setFocus();
        ui->tableView->selectRow(idx.row());
        newAddressToSelect.clear();
    }
}

void AddressBookPage::on_copyPubKey_clicked()
{
    QModelIndexList selection = ui->tableView->selectionModel()->selectedRows(AddressTableModel::Address);
    if(!selection.isEmpty())
    {
        QString addrStr = selection.at(0).data(Qt::EditRole).toString();
        CBitcoinAddress address(addrStr.toStdString());
        CKeyID keyID;
        if ( !address.GetKeyID(keyID) )
        {
            QMessageBox::warning(this, windowTitle(),
                tr("Address \"%1\" doesn't have public key ").arg(addrStr),
                QMessageBox::Ok, QMessageBox::Ok);
            return;
        }
        CPubKey vchPubKey;
        if ( !pwalletMain->GetPubKey(keyID, vchPubKey))
        {
            QMessageBox::warning(this, windowTitle(),
                tr("Address \"%1\" doesn't have public key ").arg(addrStr),
                QMessageBox::Ok, QMessageBox::Ok);
            return;
        }
        GUIUtil::setClipboard(QString::fromStdString(HexStr(vchPubKey)));
    }
}

void AddressBookPage::on_copyPriKey_clicked()
{
    QModelIndexList selection = ui->tableView->selectionModel()->selectedRows(AddressTableModel::Address);
    if(!selection.isEmpty())
    {
        QString addrStr = selection.at(0).data(Qt::EditRole).toString();
        CBitcoinAddress address(addrStr.toStdString());
        CKeyID keyID;
        if ( !address.GetKeyID(keyID) )
        {
            QMessageBox::warning(this, windowTitle(),
                tr("Address \"%1\" doesn't have public key ").arg(addrStr),
                QMessageBox::Ok, QMessageBox::Ok);
            return;
        }
        //CSecret vchSecret;
	      //bool fCompressed;
	      CKey key;
	      //if (!pwalletMain->GetSecret(keyID, vchSecret, fCompressed)) 
	      if (!pwalletMain->GetKey(keyID, key))
	      {
	          QMessageBox::warning(this, windowTitle(),
	              tr("Address \"%1\" doesn't have private key ").arg(addrStr),
	              QMessageBox::Ok, QMessageBox::Ok);
	          return;
	      }
        //GUIUtil::setClipboard(CBitcoinSecret(vchSecret, fCompressed).ToString().c_str());
        GUIUtil::setClipboard(CBitcoinSecret(key).ToString().c_str());
    }
}

void AddressBookPage::on_signMessage_clicked()
{
    QTableView *table = ui->tableView;
    QModelIndexList indexes = table->selectionModel()->selectedRows(AddressTableModel::Address);

    Q_FOREACH (const QModelIndex index, indexes)
    {
        QString address = index.data().toString();
        //emit signMessage(address);
		    SignVerifyMessageDialog *signVerifyMessageDialog = new SignVerifyMessageDialog(this);
		    signVerifyMessageDialog->setAttribute(Qt::WA_DeleteOnClose);
		    //signVerifyMessageDialog->setModel(walletModel);
		    signVerifyMessageDialog->showTab_SM(true);
		
		    if (!address.isEmpty())
		        signVerifyMessageDialog->setAddress_SM(address);
    }
}

void AddressBookPage::on_verifyMessage_clicked()
{
    QTableView *table = ui->tableView;
    QModelIndexList indexes = table->selectionModel()->selectedRows(AddressTableModel::Address);

    Q_FOREACH (const QModelIndex index, indexes)
    {
        QString address = index.data().toString();
        //emit verifyMessage(address);
		    SignVerifyMessageDialog *signVerifyMessageDialog = new SignVerifyMessageDialog(this);
		    signVerifyMessageDialog->setAttribute(Qt::WA_DeleteOnClose);
		    //signVerifyMessageDialog->setModel(walletModel);
		    signVerifyMessageDialog->showTab_VM(true);
		
		    if (!address.isEmpty())
		        signVerifyMessageDialog->setAddress_VM(address);
    }
}

void AddressBookPage::onSendCoinsAction()
{
    QTableView *table = ui->tableView;
    QModelIndexList indexes = table->selectionModel()->selectedRows(AddressTableModel::Address);

    Q_FOREACH (const QModelIndex index, indexes)
    {
        QString address = index.data().toString();
        Q_EMIT sendCoins(address);
        close();
    }
}

void AddressBookPage::createAddress()
{
    CreateMultiSigAddrDialog dlg(this);
    if(dlg.exec())
    {
    }
}

bool writeString(const QString &filename, const QString& hex);
void AddressBookPage::exportAddress()
{
    QString s;
    QTableView *table = ui->tableView;
    QModelIndexList indexes = table->selectionModel()->selectedRows(AddressTableModel::Address);

    //foreach (QModelIndex index, indexes)
    s = indexes[0].data().toString();

    CBitcoinAddress address(s.toStdString());
    
    CScript subscript;
    CScriptID scriptID;
    address.GetScriptID(scriptID);
    pwalletMain->GetCScript(scriptID, subscript);

    json_spirit::Object addrJson;
    addrJson.push_back(json_spirit::Pair("address", address.ToString()));
    addrJson.push_back(json_spirit::Pair("redeemScript", HexStr(subscript.begin(), subscript.end())));
    std::string ss = json_spirit::write_string(json_spirit::Value(addrJson), false);
    QString addrJsonStr = QString::fromStdString(ss);

    QString filename = GUIUtil::getSaveFileName(
            this,
            tr("Save MultiSig Address"), QString(),
            tr("MultiSig Address file (*.msa)"), NULL);

    if (filename.isNull()) return;

    if(!writeString(filename, addrJsonStr))
    {
        QMessageBox::critical(this, tr("Exporting Failed"), tr("Could not write to file %1.").arg(filename),
                              QMessageBox::Abort, QMessageBox::Abort);
    }
}


void AddressBookPage::on_showQRCode_clicked()
{
#ifdef USE_QRCODE
    if(!model)
        return;
        
    QTableView *table = ui->tableView;
    QModelIndexList indexes = table->selectionModel()->selectedRows(AddressTableModel::Address);

    Q_FOREACH (const QModelIndex index, indexes)
    {
        QString address = index.data().toString();
        QString label = index.sibling(index.row(), 0).data(Qt::EditRole).toString();

        QRCodeDialog *dialog = new QRCodeDialog(address, label, tab == ReceivingTab, this);
        dialog->setModel(optionsModel);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    }
#endif
}

void AddressBookPage::on_importQRCodeButton_clicked()
{
#ifdef USE_ZXING
    SnapWidget* snap = new SnapWidget(this);
    connect(snap, SIGNAL(finished(QString)), this, SLOT(onSnapClosed(QString))); 
#endif
}

void AddressBookPage::onSnapClosed(QString privKey)
{
    if (privKey.size() > 0)
        //to do : some more parsing and validation is needed here
        //todo: prompt for a label
        //todo: display a dialog if it doesn't work
        Q_EMIT importWallet(privKey);
}
