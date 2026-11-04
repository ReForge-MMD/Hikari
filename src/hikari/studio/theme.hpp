#pragma once

#include <QString>

namespace hikari::studio {

class NordicTheme {
public:
    static QString style_sheet() {
        return QStringLiteral(R"(
QWidget {
    background: #181b22;
    color: #dce0e6;
    font-family: "Segoe UI", "Yu Gothic UI", sans-serif;
    font-size: 9pt;
}
QMainWindow::separator { background: #383f4c; width: 1px; height: 1px; }
QMenuBar { background: #20252e; border-bottom: 1px solid #383f4c; padding: 2px; }
QMenuBar::item { padding: 5px 10px; background: transparent; }
QMenuBar::item:selected { background: #2d3742; }
QMenu { background: #20252e; border: 1px solid #383f4c; padding: 4px; }
QMenu::item { padding: 5px 24px 5px 18px; }
QMenu::item:selected { background: #2d3742; }
QMenu::separator { height: 1px; background: #383f4c; margin: 4px 8px; }
QDockWidget { titlebar-close-icon: none; titlebar-normal-icon: none; }
QDockWidget::title { background: #20252e; border: 1px solid #383f4c; padding: 5px; }
QTabWidget::pane { border: 1px solid #383f4c; background: #181b22; }
QTabBar::tab { background: #20252e; border: 1px solid #383f4c; padding: 5px 12px; margin-right: 2px; }
QTabBar::tab:selected { background: #2d3742; color: #7ec8e3; border-bottom-color: #7ec8e3; }
QPushButton { background: #2d3742; border: 1px solid #4a5361; border-radius: 4px; padding: 5px 10px; }
QPushButton:hover { background: #37424f; border-color: #7ec8e3; }
QPushButton:pressed { background: #20252e; }
QPushButton:disabled { color: #6b7280; }
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background: #11141a;
    border: 1px solid #4a5361;
    border-radius: 3px;
    padding: 3px 5px;
    selection-background-color: #4d6d7e;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: #7ec8e3; }
QComboBox QAbstractItemView { background: #20252e; border: 1px solid #4a5361; selection-background-color: #2d3742; }
QCheckBox::indicator, QRadioButton::indicator { width: 14px; height: 14px; }
QCheckBox::indicator:unchecked { border: 1px solid #4a5361; background: #11141a; border-radius: 2px; }
QCheckBox::indicator:checked { border: 1px solid #7ec8e3; background: #7ec8e3; border-radius: 2px; }
QSlider::groove:horizontal { height: 4px; background: #383f4c; border-radius: 2px; }
QSlider::handle:horizontal { background: #7ec8e3; width: 12px; margin: -4px 0; border-radius: 6px; }
QSlider::sub-page:horizontal { background: #4d6d7e; border-radius: 2px; }
QListWidget, QTreeWidget, QTableWidget {
    background: #11141a;
    border: 1px solid #383f4c;
    alternate-background-color: #181b22;
}
QListWidget::item:selected { background: #2d3742; color: #7ec8e3; }
QScrollArea { border: none; }
QScrollBar:vertical { background: #181b22; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: #4a5361; min-height: 24px; border-radius: 5px; }
QScrollBar::handle:vertical:hover { background: #7ec8e3; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QScrollBar:horizontal { background: #181b22; height: 10px; margin: 0; }
QScrollBar::handle:horizontal { background: #4a5361; min-width: 24px; border-radius: 5px; }
QStatusBar { background: #20252e; border-top: 1px solid #383f4c; }
QStatusBar QLabel { background: transparent; }
QGroupBox { border: 1px solid #383f4c; border-radius: 5px; margin-top: 9px; padding-top: 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: #7ec8e3; }
QLabel#SectionLabel { color: #7ec8e3; font-weight: bold; }
QLabel#HintLabel { color: #8c97a5; }
)");
    }
};

} // namespace hikari::studio
