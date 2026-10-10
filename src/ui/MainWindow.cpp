#include "ui/MainWindow.h"

#include <QColor>
#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/TestRunner.h"
#include "core/Verdict.h"
#include "ui/Language.h"

namespace {

enum Column { ColStatus, ColTest, ColSummary, ColCount };

// What the status cell shows, kept in the cell (Qt::UserRole) so its text
// can be rebuilt when the language changes: a Status, or one of these.
constexpr int kWaiting = -1;
constexpr int kRunning = -2;

QColor colorFor(Status s)
{
    switch (s) {
    case Status::Pass:    return QColor("#c8e6c9");
    case Status::Warn:    return QColor("#ffe0b2");
    case Status::Fail:    return QColor("#ffcdd2");
    case Status::Error:   return QColor("#e1bee7");
    case Status::Skipped: return QColor("#eeeeee");
    }
    return QColor("#ffffff");
}

void setCell(QTableWidget* table, int row, int col, const QString& text, const QColor& bg)
{
    auto* item = table->item(row, col);
    if (!item) {
        item = new QTableWidgetItem;
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, col, item);   // the table takes ownership
    }
    item->setText(text);
    item->setBackground(bg);
    item->setForeground(QColor("#000000"));   // readable on any desktop theme
}

} // namespace

MainWindow::MainWindow(TestRunner& runner, QWidget* parent)
    : QMainWindow(parent), m_runner(runner)
{
    resize(900, 600);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    auto* top = new QHBoxLayout;
    m_startButton = new QPushButton(central);
    m_startButton->setMinimumHeight(48);
    top->addWidget(m_startButton, 1);

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
    m_verdict->setMinimumHeight(56);
    layout->addWidget(m_verdict);

    setCentralWidget(central);
    resetRows();
    retranslateUi();

    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(m_language, &QComboBox::currentIndexChanged, this,
            [this] { setLanguage(m_language->currentData().toString()); });
    connect(&m_runner, &TestRunner::testStarted, this, &MainWindow::onTestStarted);
    connect(&m_runner, &TestRunner::testFinished, this, &MainWindow::onTestFinished);
    connect(&m_runner, &TestRunner::allFinished, this, &MainWindow::onAllFinished);
    connect(m_table, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) { showDetailsOf(row); });
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
    setWindowTitle(tr("Oxfam PC Tester"));
    m_startButton->setText(tr("START"));
    m_table->setHorizontalHeaderLabels({tr("Status"), tr("Test"), tr("Summary")});
    m_details->setPlaceholderText(tr("Select a test to see the raw details."));
    for (int i = 0; i < m_runner.count(); ++i) {
        m_table->item(i, ColTest)->setText(m_runner.test(i).name());
        setStatusCell(i, m_table->item(i, ColStatus)->data(Qt::UserRole).toInt());
    }
    if (!m_verdict->text().isEmpty())
        m_verdict->setText(computeVerdict(m_statuses).text);
}

void MainWindow::setStatusCell(int row, int state)
{
    if (state == kWaiting) {
        setCell(m_table, row, ColStatus, tr("WAITING"), QColor("#ffffff"));
    } else if (state == kRunning) {
        setCell(m_table, row, ColStatus, tr("RUNNING"), QColor("#bbdefb"));
    } else {
        const auto s = static_cast<Status>(state);
        setCell(m_table, row, ColStatus, statusLabel(s), colorFor(s));
    }
    m_table->item(row, ColStatus)->setData(Qt::UserRole, state);
}

void MainWindow::resetRows()
{
    for (int i = 0; i < m_runner.count(); ++i) {
        setStatusCell(i, kWaiting);
        setCell(m_table, i, ColTest, m_runner.test(i).name(), QColor("#ffffff"));
        setCell(m_table, i, ColSummary, QString(), QColor("#ffffff"));
        m_table->item(i, ColTest)->setData(Qt::UserRole, QString());
    }
    m_details->clear();
    m_verdict->clear();
    m_verdict->setStyleSheet(QString());
    m_statuses.clear();
}

void MainWindow::onStartClicked()
{
    resetRows();
    m_startButton->setEnabled(false);
    m_language->setEnabled(false);   // the language is chosen before a run
    m_runner.startAll();
}

void MainWindow::onTestStarted(int row)
{
    setStatusCell(row, kRunning);
}

void MainWindow::onTestFinished(int row, const TestResult& result)
{
    const QColor bg = colorFor(result.status);
    setStatusCell(row, static_cast<int>(result.status));
    setCell(m_table, row, ColSummary, result.summary, bg);
    m_table->item(row, ColTest)->setData(Qt::UserRole, result.details);
    m_statuses.push_back(result.status);
    if (m_table->currentRow() == row)
        showDetailsOf(row);
}

void MainWindow::onAllFinished()
{
    const Verdict v = computeVerdict(m_statuses);
    m_verdict->setText(v.text);
    m_verdict->setStyleSheet(
        QStringLiteral("background:%1; color:black; font-size:20px; font-weight:bold;")
            .arg(colorFor(v.level).name()));
    m_startButton->setEnabled(true);
    m_language->setEnabled(true);
}

void MainWindow::showDetailsOf(int row)
{
    if (row < 0 || !m_table->item(row, ColTest))
        return;
    m_details->setPlainText(m_table->item(row, ColTest)->data(Qt::UserRole).toString());
}
