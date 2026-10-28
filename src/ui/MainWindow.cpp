#include "ui/MainWindow.h"

#include <algorithm>

#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QStatusBar>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent>

#include "core/TestRunner.h"
#include "core/Verdict.h"
#include "report/RunReport.h"
#include "tests/sysinfo/SystemInfoTest.h"
#include "ui/Language.h"
#include "ui/Theme.h"

namespace {

enum Column { ColStatus, ColTest, ColSummary, ColCount };

// What the status cell shows, kept in the cell (Qt::UserRole) so its text
// can be rebuilt when the language changes: a Status, or one of these.
constexpr int kWaiting = -1;
constexpr int kRunning = -2;
constexpr int kNotChosen = -3;   // unticked before START: not part of this run

// Terminal look: coloured text on the dark table, not coloured cells.
void setCell(QTableWidget* table, int row, int col, const QString& text, const QColor& color,
             const QIcon& icon = {})
{
    auto* item = table->item(row, col);
    if (!item) {
        item = new QTableWidgetItem;
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, col, item);   // the table takes ownership
    }
    item->setText(text);
    item->setForeground(color);
    item->setIcon(icon);
}

} // namespace

MainWindow::MainWindow(TestRunner& runner, DeliveryLinks delivery, const std::vector<bool>& ticked, QWidget* parent)
    : QMainWindow(parent), m_runner(runner), m_delivery(std::move(delivery))
{
    resize(900, 600);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    auto* top = new QHBoxLayout;
    m_startButton = new QPushButton(central);
    m_startButton->setObjectName(QStringLiteral("startButton"));   // bigger, see Theme.cpp
    m_startButton->setMinimumHeight(48);
    top->addWidget(m_startButton, 1);

    // Runs the selected row alone: an ERROR row, a key pressed too late, or
    // one part checked again after a repair (battery changed, PC restarted:
    // no need to redo everything). Then the verdict says PARTIAL.
    m_runAgainButton = new QPushButton(central);
    m_runAgainButton->setMinimumHeight(48);
    top->addWidget(m_runAgainButton);

    // Once some test ran: only the rows with a result are written.
    m_deliveryButton = new QPushButton(central);
    m_deliveryButton->setMinimumHeight(48);
    m_deliveryButton->setEnabled(false);
    top->addWidget(m_deliveryButton);

    // Each language in its own name, never translated: someone who does not
    // understand the current language must still find theirs. \u00e7 keeps
    // this source file ASCII (MSVC C4819).
    m_language = new QComboBox(central);
    m_language->addItem(QStringLiteral("Fran\u00e7ais"), QStringLiteral("fr"));
    m_language->addItem(QStringLiteral("Nederlands"), QStringLiteral("nl"));
    m_language->addItem(QStringLiteral("English"), QStringLiteral("en"));
    m_language->setCurrentIndex(m_language->findData(currentLanguage()));
    m_language->setMinimumHeight(48);
    top->addWidget(m_language);
    layout->addLayout(top);

    // Which PC this is, read at launch: known before START, for the run
    // report and the delivery sheet, and never part of the verdict.
    m_pcInfo = new QLabel(central);
    m_pcInfo->setObjectName(QStringLiteral("pcInfo"));   // framed, see Theme.cpp
    m_pcInfo->setWordWrap(true);
    m_pcInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);   // the serial can be copied
    layout->addWidget(m_pcInfo);

    m_table = new QTableWidget(m_runner.count(), ColCount, central);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_table, 3);

    m_details = new QPlainTextEdit(central);
    m_details->setReadOnly(true);
    layout->addWidget(m_details, 1);

    m_verdict = new QLabel(central);
    m_verdict->setAlignment(Qt::AlignCenter);
    m_verdict->setWordWrap(true);   // the French "INCOMPLET..." verdict is wider than the window
    m_verdict->setMinimumHeight(56);
    layout->addWidget(m_verdict);

    setCentralWidget(central);
    resetRows();
    // A tick box per test: START runs the ticked ones.
    for (int i = 0; i < m_runner.count(); ++i) {
        QTableWidgetItem* item = m_table->item(i, ColTest);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        const auto index = static_cast<size_t>(i);
        item->setCheckState(index >= ticked.size() || ticked[index] ? Qt::Checked : Qt::Unchecked);
    }
    retranslateUi();

    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(m_runAgainButton, &QPushButton::clicked, this, &MainWindow::onRunAgainClicked);
    connect(m_deliveryButton, &QPushButton::clicked, this, &MainWindow::onDeliveryClicked);
    connect(m_language, &QComboBox::currentIndexChanged, this,
            [this] { setLanguage(m_language->currentData().toString()); });
    connect(&m_runner, &TestRunner::testStarted, this, &MainWindow::onTestStarted);
    connect(&m_runner, &TestRunner::testFinished, this, &MainWindow::onTestFinished);
    connect(&m_runner, &TestRunner::allFinished, this, &MainWindow::onAllFinished);
    connect(m_table, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        showDetailsOf(row);
        updateButtons();
    });
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (item->column() == ColTest)
            updateButtons();   // a box (un)ticked: START needs at least one
    });
    updateButtons();

    // Milliseconds since PowerShell is gone, but still file reads and system
    // calls: on a worker thread, never on the UI thread.
    connect(&m_pcWatcher, &QFutureWatcher<SystemInfo>::finished, this, [this] {
        m_pc = m_pcWatcher.result();
        // The screen the window opened on: the PC's own screen, unless an
        // external monitor is the main one.
        if (const QScreen* s = screen()) {
            m_pc.screenMm = s->physicalSize();
            m_pc.screenPixels = s->size() * s->devicePixelRatio();   // native pixels, not scaled ones
        }
        m_pcRead = true;
        showPcInfo();
    });
    m_pcWatcher.setFuture(QtConcurrent::run(&readSystemInfo));
}

// setLanguage() makes Qt send LanguageChange to every widget.
void MainWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
        retranslateUi();
    QMainWindow::changeEvent(event);
}

// Every text that tr() gives, set again in the current language. Summaries
// and details of tests already run are not here: they stay as they were made.
void MainWindow::retranslateUi()
{
    setWindowTitle(tr("Lazy Djin's Tester") + QLatin1Char(' ') + QCoreApplication::applicationVersion());
    m_startButton->setText(tr("START"));
    m_runAgainButton->setText(tr("Run this test"));
    m_deliveryButton->setText(tr("Fill the delivery sheet"));
    m_table->setHorizontalHeaderLabels({tr("Status"), tr("Test"), tr("Summary")});
    m_details->setPlaceholderText(tr("Select a test to see the raw details."));
    showPcInfo();
    for (int i = 0; i < m_runner.count(); ++i) {
        m_table->item(i, ColTest)->setText(m_runner.test(i).name());
        setStatusCell(i, m_table->item(i, ColStatus)->data(Qt::UserRole).toInt());
    }
    if (!m_verdict->text().isEmpty())
        m_verdict->setText(verdict().text);
}

void MainWindow::showPcInfo()
{
    if (!m_pcRead) {
        m_pcInfo->setText(tr("This PC: reading..."));
        return;
    }
    const TestResult pc = SystemInfoTest::evaluate(m_pc);
    m_pcInfo->setText(tr("This PC: %1").arg(pc.summary));
    m_pcInfo->setToolTip(pc.details);
    // Normal text when read; the ERROR colour if not (the model is then asked
    // for when the delivery sheet is filled).
    const QColor color = pc.status == Status::Pass ? Theme::text() : Theme::statusColor(pc.status);
    m_pcInfo->setStyleSheet(QStringLiteral("color:%1;").arg(color.name()));
}

void MainWindow::setStatusCell(int row, int state)
{
    if (state == kWaiting) {
        setCell(m_table, row, ColStatus, tr("WAITING"), Theme::waitingColor());
    } else if (state == kNotChosen) {
        setCell(m_table, row, ColStatus, tr("NOT CHOSEN"), Theme::dimText());
    } else if (state == kRunning) {
        setCell(m_table, row, ColStatus, tr("RUNNING"), Theme::runningColor(), Theme::runningIcon());
    } else {
        const auto s = static_cast<Status>(state);
        setCell(m_table, row, ColStatus, statusLabel(s), Theme::statusColor(s), Theme::statusIcon(s));
    }
    m_table->item(row, ColStatus)->setData(Qt::UserRole, state);
}

void MainWindow::resetRows()
{
    for (int i = 0; i < m_runner.count(); ++i) {
        setStatusCell(i, kWaiting);
        setCell(m_table, i, ColTest, m_runner.test(i).name(), Theme::text());
        setCell(m_table, i, ColSummary, QString(), Theme::text());
        m_table->item(i, ColTest)->setData(Qt::UserRole, QString());
    }
    m_details->clear();
    m_verdict->clear();
    m_verdict->setStyleSheet(QString());
    m_results.assign(static_cast<size_t>(m_runner.count()), TestResult{});
    m_ran.assign(static_cast<size_t>(m_runner.count()), false);
}

// One place decides which buttons work: nothing during a run (the language
// is chosen before it); one test needs a selected row, the sheet a result.
void MainWindow::updateButtons()
{
    const bool idle = !m_runner.isRunning();
    m_startButton->setEnabled(idle && !chosenRows().empty());
    m_runAgainButton->setEnabled(idle && m_table->currentRow() >= 0);
    m_deliveryButton->setEnabled(idle && anyRan());
    m_language->setEnabled(idle);
}

// Statuses of the rows that ran: a row never run is not a pass.
std::vector<Status> MainWindow::statuses() const
{
    std::vector<Status> ran;
    for (size_t i = 0; i < m_results.size(); ++i) {
        if (m_ran[i])
            ran.push_back(m_results[i].status);
    }
    return ran;
}

Verdict MainWindow::verdict() const
{
    return computeVerdict(statuses(), m_runner.count());
}

std::vector<int> MainWindow::chosenRows() const
{
    std::vector<int> rows;
    for (int i = 0; i < m_runner.count(); ++i) {
        const QTableWidgetItem* item = m_table->item(i, ColTest);
        if (item && item->checkState() == Qt::Checked)
            rows.push_back(i);
    }
    return rows;
}

bool MainWindow::anyRan() const
{
    return std::find(m_ran.begin(), m_ran.end(), true) != m_ran.end();
}

void MainWindow::onStartClicked()
{
    const std::vector<int> rows = chosenRows();
    resetRows();
    // The unticked rows say so; they stay out of the verdict ("PARTIAL"),
    // the run report and the delivery sheet, like a row never run.
    for (int i = 0; i < m_runner.count(); ++i) {
        if (std::find(rows.begin(), rows.end(), i) == rows.end())
            setStatusCell(i, kNotChosen);
    }
    m_runner.start(rows);
    updateButtons();
}

void MainWindow::onRunAgainClicked()
{
    const int row = m_table->currentRow();
    if (row < 0)
        return;
    // The old verdict no longer holds; it comes back when this test answers.
    m_verdict->clear();
    m_verdict->setStyleSheet(QString());
    m_runner.startOne(row);
    updateButtons();
}

void MainWindow::onTestStarted(int row)
{
    setStatusCell(row, kRunning);
}

void MainWindow::onTestFinished(int row, const TestResult& result)
{
    setStatusCell(row, static_cast<int>(result.status));
    setCell(m_table, row, ColSummary, result.summary, Theme::text());
    m_table->item(row, ColTest)->setData(Qt::UserRole, result.details);
    m_results[static_cast<size_t>(row)] = result;
    m_ran[static_cast<size_t>(row)] = true;
    if (m_table->currentRow() == row)
        showDetailsOf(row);
}

void MainWindow::onAllFinished()
{
    const Verdict v = verdict();
    m_verdict->setText(v.text);
    m_verdict->setStyleSheet(
        QStringLiteral("color:%1; border:2px solid %1; font-size:18pt; font-weight:bold; padding:6px;")
            .arg(Theme::statusColor(v.level).name()));
    updateButtons();
    saveReport(v);
}

// Every row's details in a text file, after each run and each "run again"
// (one file per run, never overwritten). Automatic: no dialog, the status bar
// says where it went or why it could not.
void MainWindow::saveReport(const Verdict& verdict)
{
    RunReport report;
    report.when = QDateTime::currentDateTime();
    report.version = QCoreApplication::applicationVersion();
    report.verdict = verdict.text;
    if (m_pcRead) {   // first section: which PC
        const TestResult pc = SystemInfoTest::evaluate(m_pc);
        report.model = pc.note;
        report.rows.push_back({tr("This PC"), pc});
    }
    // Rows never run are left out: the verdict line says PARTIAL.
    for (int i = 0; i < m_runner.count(); ++i) {
        if (m_ran[static_cast<size_t>(i)])
            report.rows.push_back({m_runner.test(i).name(), m_results[static_cast<size_t>(i)]});
    }
    QString path, error;
    if (writeRunReport(report, &path, &error))
        statusBar()->showMessage(tr("Details saved to %1").arg(QDir::toNativeSeparators(path)));
    else
        statusBar()->showMessage(tr("Could not save the details (%1): %2")
                                     .arg(QDir::toNativeSeparators(path), error));
}

void MainWindow::onDeliveryClicked()
{
    DeliveryInput input;
    if (m_pcRead)
        input.systemModel = SystemInfoTest::evaluate(m_pc).note;
    for (int i = 0; i < m_runner.count(); ++i) {
        const auto it = m_delivery.rows.find(&m_runner.test(i));
        if (it != m_delivery.rows.end() && m_ran[static_cast<size_t>(i)])   // the others keep what the sheet has
            input.results[it->second] = m_results[static_cast<size_t>(i)];
    }
    DeliveryAction::run(this, input);
}

void MainWindow::showDetailsOf(int row)
{
    if (row < 0 || !m_table->item(row, ColTest))
        return;
    m_details->setPlainText(m_table->item(row, ColTest)->data(Qt::UserRole).toString());
}
