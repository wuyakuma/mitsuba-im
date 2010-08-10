#include "ui_programsettingsdlg.h"
#include "programsettingsdlg.h"
#include "addserverdlg.h"
#include <mitsuba/core/sched_remote.h>

ProgramSettingsDialog::ProgramSettingsDialog(QWidget *parent) :
		QDialog(parent),
	ui(new Ui::ProgramSettingsDialog) {
	ui->setupUi(this);
	ui->listenPort->setValidator(new QIntValidator(this));
#if defined(__OSX__)
	ui->addPathButton->setMaximumSize(24, 24);
	ui->removePathButton->setMaximumSize(24, 24);
	ui->addConnectionButton->setMaximumSize(24, 24);
	ui->removeConnectionButton->setMaximumSize(24, 24);
	ui->gridLayout1->setVerticalSpacing(6);
	ui->gridLayout2->setVerticalSpacing(6);
	ui->buttonsLayout1->setSpacing(16);
	ui->buttonsLayout2->setSpacing(16);
#endif
}

ProgramSettingsDialog::~ProgramSettingsDialog() {
	delete ui;
}
	
void ProgramSettingsDialog::setConnections(QList<ServerConnection> &connections) {
	m_connections = connections;
	for (int i=0; i<connections.size(); ++i)
		ui->connectionList->addItem(connections[i].toString());
	refresh();
}

void ProgramSettingsDialog::on_addConnectionButton_clicked() {
	AddServerDialog d(this);
	if (d.exec()) {
		ServerConnection c = d.getConnection();
		if (c.createWorker(this)) {
			c.worker->incRef();
			m_connections.append(c);
			ui->connectionList->addItem(c.toString());
		}
	}

	refresh();
}

void ProgramSettingsDialog::on_removeConnectionButton_clicked() {
	int entry = ui->connectionList->currentIndex().row();
	ServerConnection conn = m_connections.takeAt(entry);
	if (!conn.isRegistered)
		conn.worker->decRef();
	ui->connectionList->takeItem(entry);
	refresh();
}

void ProgramSettingsDialog::on_connectionList_currentItemChanged(QListWidgetItem *cur, QListWidgetItem *prev) {
	ui->removeConnectionButton->setEnabled(cur != NULL);
}

void ProgramSettingsDialog::refresh() {
	int coreCount = 0;
	for (int i=0; i<m_connections.size(); ++i)
		coreCount += m_connections[i].worker->getCoreCount();
	int totalCount = coreCount + getLocalWorkerCount();
	ui->statsLabel1->setText(tr("Cores attached via network: %1").arg(coreCount));
	ui->statsLabel2->setText(tr("Total number of attached cores: %1").arg(totalCount));
	int listenPort = ui->listenPort->text().toInt();
	ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(
		totalCount > 0 && listenPort > 0 && listenPort < 65536 &&
		ui->nodeName->text().length() > 0);
}

void ProgramSettingsDialog::on_addPathButton_clicked() {
	QString dir = QFileDialog::getExistingDirectory(this, tr("Open Directory"), "",
		QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
	if (dir != "")
		ui->searchPathList->addItem(dir);	
}

void ProgramSettingsDialog::on_removePathButton_clicked() {
	delete ui->searchPathList->takeItem(ui->searchPathList->currentIndex().row());
}

void ProgramSettingsDialog::on_searchPathList_currentItemChanged(QListWidgetItem *cur, QListWidgetItem *prev) {
	ui->removePathButton->setEnabled(cur != NULL);
}

void ProgramSettingsDialog::changeEvent(QEvent *e) {
    QDialog::changeEvent(e);
    switch (e->type()) {
    case QEvent::LanguageChange:
		ui->retranslateUi(this);
        break;
    default:
        break;
    }
}