#include "ui/MainWindow.h"

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
#include <QStatusBar>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/TestRunner.h"
#include "core/Verdict.h"
#include "report/RunReport.h"
#include "ui/Language.h"
#include "ui/Theme.h"

namespace {

enum Column { ColStatus, ColTest, ColSummary, ColCount };

// What the status cell shows, kept in the cell (Qt::UserRole) so its text
// can be rebuilt when the language changes: a Status, or one of these.
constexpr int kWaiting = -1;
constexpr int kRunning = -2;

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

MainWindow::MainWindow(TestRunner& runner, DeliveryLinks delivery, QWidget* parent)
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

    // Runs the selected row alone (an ERROR row, a key pressed too late...).
    // Only after a complete run, so the verdict always covers every test.
    m_runAgainButton = new QPushButton(central);
    m_runAgainButton->setMinimumHeight(48);
    top->addWidget(m_runAgainButton);

    // Only after a complete run: the sheet must get this PC's results.
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
    updateButtons();
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
    m_runAgainButton->setText(tr("Run this test again"));
    m_deliveryButton->setText(tr("Fill the delivery sheet"));
    m_table->setHorizontalHeaderLabels({tr("Status"), tr("Test"), tr("Summary")});
    m_details->setPlaceholderText(tr("Select a test to see the raw details."));
    for (int i = 0; i < m_runner.count(); ++i) {
        m_table->item(i, ColTest)->setText(m_runner.test(i).name());
        setStatusCell(i, m_table->item(i, ColStatus)->data(Qt::UserRole).toInt());
    }
    if (!m_verdict->text().isEmpty())
        m_verdict->setText(computeVerdict(statuses()).text);
}

void MainWindow::setStatusCell(int row, int state)
{
    if (state == kWaiting) {
        setCell(m_table, row, ColStatus, tr("WAITING"), Theme::waitingColor());
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
    m_fullRunDone = false;
}

// One place decides which buttons work: nothing during a run (the language
// is chosen before it); "again" and the sheet only after a complete run.
void MainWindow::updateButtons()
{
    const bool idle = !m_runner.isRunning();
    m_startButton->setEnabled(idle);
    m_runAgainButton->setEnabled(idle && m_fullRunDone && m_table->currentRow() >= 0);
    m_deliveryButton->setEnabled(idle && m_fullRunDone);
    m_language->setEnabled(idle);
}

std::vector<Status> MainWindow::statuses() const
{
    std::vector<Status> all;
    for (const TestResult& r : m_results)
        all.push_back(r.status);
    return all;
}

void MainWindow::onStartClicked()
{
    resetRows();
    m_runner.startAll();
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
    if (m_table->currentRow() == row)
        showDetailsOf(row);
}

void MainWindow::onAllFinished()
{
    m_fullRunDone = true;   // a single test again only runs after a complete run
    const Verdict v = computeVerdict(statuses());
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
    for (int i = 0; i < m_runner.count(); ++i) {
        const TestResult& result = m_results[static_cast<size_t>(i)];
        if (&m_runner.test(i) == m_delivery.systemInfo)
            report.model = result.note;
        report.rows.push_back({m_runner.test(i).name(), result});
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
    for (int i = 0; i < m_runner.count(); ++i) {
        const ITest* test = &m_runner.test(i);
        const TestResult& result = m_results[static_cast<size_t>(i)];
        if (test == m_delivery.systemInfo)
            input.systemModel = result.note;
        const auto it = m_delivery.rows.find(test);
        if (it != m_delivery.rows.end())
            input.results[it->second] = result;
    }
    DeliveryAction::run(this, input);
}

void MainWindow::showDetailsOf(int row)
{
    if (row < 0 || !m_table->item(row, ColTest))
        return;
    m_details->setPlainText(m_table->item(row, ColTest)->data(Qt::UserRole).toString());
}
