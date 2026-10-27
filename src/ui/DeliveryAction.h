#pragma once
#include <map>
#include <QCoreApplication>
#include "core/TestResult.h"
#include "delivery/DeliverySheet.h"

class ITest;
class QWidget;

// Which test answers which row of the delivery sheet. Built in main.cpp,
// the only place that knows the concrete tests. (The model comes from the
// "This PC" panel, not from a test.)
struct DeliveryLinks {
    std::map<const ITest*, SheetRow> rows;
};

// What one run gives to the sheet.
struct DeliveryInput {
    QString systemModel;                            // as the PC reports it
    std::map<SheetRow, TestResult> results;
};

// The "Fill the delivery sheet" button: finds the sheet on the Desktop (or
// copies it there from <root>/delivery-sheets/), asks for the model when it
// is not in the list and for the shop, adds new names to the shared sheets,
// writes, and says what happened. Every question and message is a dialog
// over parent.
class DeliveryAction {
    Q_DECLARE_TR_FUNCTIONS(DeliveryAction)   // tr() without being a QObject
public:
    static void run(QWidget* parent, const DeliveryInput& input);
};
