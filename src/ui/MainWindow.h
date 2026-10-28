#pragma once
#include <vector>
#include <QFutureWatcher>
#include <QMainWindow>
#include "core/TestResult.h"
#include "tests/sysinfo/SystemInfo.h"
#include "ui/DeliveryAction.h"

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class TestRunner;
struct Verdict;

// Pure view: it displays what the runner reports and contains no test logic.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // ticked: per runner index, the tick box at start (config/tests.json);
    // missing entries are ticked.
    MainWindow(TestRunner& runner, DeliveryLinks delivery, const std::vector<bool>& ticked = {},
               QWidget* parent = nullptr);

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslateUi();
    void showPcInfo();
    void setStatusCell(int row, int state);
    void onStartClicked();
    void onRunAgainClicked();
    void onRememberClicked();
    void updateButtons();
    std::vector<Status> statuses() const;
    Verdict verdict() const;
    bool anyRan() const;
    std::vector<int> chosenRows() const;   // ticked rows, in order
    void onTestStarted(int row);
    void onTestFinished(int row, const TestResult& result);
    void onAllFinished();
    void saveReport(const Verdict& verdict);
    void onDeliveryClicked();
    void showDetailsOf(int row);
    void resetRows();

    TestRunner& m_runner;
    QPushButton* m_startButton = nullptr;
    QPushButton* m_runAgainButton = nullptr;
    QPushButton* m_rememberButton = nullptr;
    QComboBox* m_language = nullptr;
    QLabel* m_pcInfo = nullptr;
    QPushButton* m_deliveryButton = nullptr;
    QTableWidget* m_table = nullptr;
    QPlainTextEdit* m_details = nullptr;
    QLabel* m_verdict = nullptr;
    std::vector<TestResult> m_results;   // per row, latest result: verdict and delivery sheet
    std::vector<bool> m_ran;             // per row: has a result (a test can run alone)
    DeliveryLinks m_delivery;
    // "This PC": read once at launch. Kept raw, so the panel can be
    // rewritten in another language.
    QFutureWatcher<SystemInfo> m_pcWatcher;
    SystemInfo m_pc;
    bool m_pcRead = false;
};
