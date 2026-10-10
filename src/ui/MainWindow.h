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
    void onTestStarted(int row);
    void onTestFinished(int row, const TestResult& result);
    void onAllFinished();
    void onDeliveryClicked();
    void showDetailsOf(int row);
    void resetRows();

    TestRunner& m_runner;
    QPushButton* m_startButton = nullptr;
    QComboBox* m_language = nullptr;
    QPushButton* m_deliveryButton = nullptr;
    QTableWidget* m_table = nullptr;
    QPlainTextEdit* m_details = nullptr;
    QLabel* m_verdict = nullptr;
    std::vector<Status> m_statuses;
    std::vector<TestResult> m_results;   // per row, for the delivery sheet
    DeliveryLinks m_delivery;
};
