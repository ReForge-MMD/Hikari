#pragma once

#include <QString>

namespace hikari::studio {

class NordicTheme {
public:
    static QString style_sheet() {
        return QStringLiteral(R"(
QWidget {
    background: #1b1d21;
    color: #d5d9de;
    font-family: "Segoe UI", "Yu Gothic UI", sans-serif;
    font-size: 9pt;
}
QMainWindow { background: #15171a; }
QMainWindow::separator { background: #3a3f47; width: 1px; height: 1px; }
QMenuBar { background: #23262b; border-bottom: 1px solid #3a3f47; padding: 1px; }
QMenuBar::item { padding: 4px 9px; background: transparent; }
QMenuBar::item:selected { background: #343a42; }
QMenu { background: #23262b; border: 1px solid #3a3f47; padding: 3px; }
QMenu::item { padding: 4px 22px 4px 16px; }
QMenu::item:selected { background: #343a42; }
QMenu::item:disabled { color: #6a727b; }
QMenu::separator { height: 1px; background: #3a3f47; margin: 3px 8px; }
QDockWidget { border: 1px solid #3a3f47; titlebar-close-icon: none; titlebar-normal-icon: none; }
QDockWidget::title { background: #23262b; border-bottom: 1px solid #3a3f47; padding: 3px 6px; text-align: left; }
QTabWidget::pane { border: 1px solid #3a3f47; background: #1b1d21; }
QTabBar::tab { background: #23262b; border: 1px solid #3a3f47; padding: 4px 10px; margin-right: 2px; }
QTabBar::tab:selected { background: #2b2f35; color: #9fc4de; border-bottom-color: #6f9fc4; }
QPushButton {
    background: #2b2f35;
    border: 1px solid #3d434c;
    border-radius: 3px;
    padding: 3px 8px;
    min-height: 16px;
}
QPushButton:hover { background: #343a42; border-color: #6f9fc4; }
QPushButton:pressed { background: #22252a; }
QPushButton:checked { background: #3a4b5c; border-color: #6f9fc4; color: #e6f1fa; }
QPushButton:disabled { color: #6a727b; border-color: #2e333a; }
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background: #15171a;
    border: 1px solid #3d434c;
    border-radius: 2px;
    padding: 1px 4px;
    min-height: 17px;
    selection-background-color: #4a6577;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: #6f9fc4; }
QComboBox::drop-down { border: none; width: 14px; }
QComboBox QAbstractItemView {
    background: #23262b;
    border: 1px solid #3d434c;
    selection-background-color: #343a42;
}
QCheckBox, QRadioButton { spacing: 5px; }
QCheckBox::indicator, QRadioButton::indicator { width: 13px; height: 13px; }
QCheckBox::indicator:unchecked { border: 1px solid #4a5361; background: #15171a; border-radius: 2px; }
QCheckBox::indicator:checked { border: 1px solid #6f9fc4; background: #6f9fc4; border-radius: 2px; }
QSlider::groove:horizontal { height: 4px; background: #343a42; border-radius: 2px; }
QSlider::handle:horizontal { background: #9fc4de; width: 10px; margin: -4px 0; border-radius: 5px; }
QSlider::sub-page:horizontal { background: #4a6577; border-radius: 2px; }
QListWidget, QTreeWidget, QTableWidget {
    background: #15171a;
    border: 1px solid #343a42;
    alternate-background-color: #1b1d21;
}
QListWidget::item:selected { background: #343a42; color: #9fc4de; }
QScrollArea { border: none; }
QScrollArea#PanelStrip { background: #15171a; }
QScrollBar:vertical { background: #1b1d21; width: 9px; margin: 0; }
QScrollBar::handle:vertical { background: #4a5361; min-height: 22px; border-radius: 4px; }
QScrollBar::handle:vertical:hover { background: #6f9fc4; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar:horizontal { background: #1b1d21; height: 9px; margin: 0; }
QScrollBar::handle:horizontal { background: #4a5361; min-width: 22px; border-radius: 4px; }
QScrollBar::handle:horizontal:hover { background: #6f9fc4; }
QStatusBar { background: #23262b; border-top: 1px solid #3a3f47; }
QStatusBar QLabel { background: transparent; }
QGroupBox { border: 1px solid #3a3f47; border-radius: 4px; margin-top: 8px; padding-top: 7px; }
QGroupBox::title { subcontrol-origin: margin; left: 7px; padding: 0 4px; color: #9fc4de; }
QLabel#SectionLabel { color: #9db8cc; font-weight: bold; }
QLabel#HintLabel { color: #7f8892; }
QLabel#FieldLabel { color: #9aa3ad; }
QLabel#PanelTitle {
    background: #2b2f35;
    color: #c2c9d1;
    padding: 2px 6px;
    border-bottom: 1px solid #3a3f47;
    font-weight: 600;
}
QWidget#MmdPanel { background: #1e2126; border: 1px solid #3a3f47; }
QWidget#ViewportArea { background: #0d0e10; }
QWidget#ViewportStrip { background: #23262b; }
QLabel#ViewportTitle { color: #c7d0d9; font-weight: 600; background: transparent; }
QPushButton#ViewportChip {
    background: #2b2f35;
    border: 1px solid #3d434c;
    border-radius: 3px;
    padding: 1px 6px;
    min-height: 16px;
}
QPushButton#ViewportChip:hover { background: #343a42; border-color: #6f9fc4; }
QPushButton#ViewportChip:checked { background: #3a4b5c; border-color: #6f9fc4; color: #e6f1fa; }
QPushButton#ViewportMode {
    background: #3a4b5c;
    border: 1px solid #6f9fc4;
    border-radius: 3px;
    padding: 1px 6px;
    color: #e6f1fa;
    font-weight: 600;
}
QPushButton#ViewportMode:hover { background: #46596d; }
QDoubleSpinBox#ViewportField, QSpinBox#ViewportField {
    background: #15171a;
    border: 1px solid #3d434c;
    border-radius: 2px;
    padding: 0 3px;
    min-height: 18px;
    max-height: 20px;
}
QLabel#ViewportCaption { color: #8a939e; background: transparent; }
QWidget#ViewGizmo { background: #1b1e24; border: 1px solid #4a4f59; border-radius: 4px; }
QToolTip { background: #2b2f35; color: #d5d9de; border: 1px solid #4a4f59; padding: 3px; }
)");
    }
};

} // namespace hikari::studio
