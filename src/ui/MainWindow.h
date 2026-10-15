#pragma once
#include <vector>
#include <QMainWindow>
#include "core/TestResult.h"
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
    MainWindow(TestRunner& runner, DeliveryLinks delivery, QWidget* parent = nullptr);

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslateUi();
    void setStatusCell(int row, int state);
    void onStartClicked();
    void onRunAgainClicked();
    void updateButtons();
    std::vector<Status> statuses() const;
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
    QComboBox* m_language = nullptr;
    QPushButton* m_deliveryButton = nullptr;
    QTableWidget* m_table = nullptr;
    QPlainTextEdit* m_details = nullptr;
    QLabel* m_verdict = nullptr;
    std::vector<TestResult> m_results;   // per row, latest result: verdict and delivery sheet
    bool m_fullRunDone = false;          // every row has a result
    DeliveryLinks m_delivery;
};
