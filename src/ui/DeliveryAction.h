#pragma once
#include <map>
#include <QCoreApplication>
#include "core/TestResult.h"
#include "delivery/DeliverySheet.h"

class ITest;
class QWidget;

// Which test answers which part of the delivery sheet. Built in main.cpp,
// the only place that knows the concrete tests.
struct DeliveryLinks {
    const ITest* systemInfo = nullptr;              // its note = "vendor model"
    std::map<const ITest*, SheetRow> rows;
};

// What one run gives to the sheet.
struct DeliveryInput {
    QString systemModel;                            // as the PC reports it
    std::map<SheetRow, TestResult> results;
};

// The "Fill the delivery sheet" button: finds the sheet on the Desktop,
// asks for the model when it is not in the list, writes, and says what
// happened. Every question and message is a dialog over parent.
class DeliveryAction {
    Q_DECLARE_TR_FUNCTIONS(DeliveryAction)   // tr() without being a QObject
public:
    static void run(QWidget* parent, const DeliveryInput& input);
};
