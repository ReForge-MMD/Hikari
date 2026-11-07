#pragma once

#include <QColor>
#include <QPalette>
#include <QString>

namespace hikari::studio {

// Material Design 3 role tokens in a cold Nordic graphite palette. The chrome
// stays neutral so the rendered model and the timeline carry the color.
namespace colors {
inline constexpr unsigned int surface = 0x101419;
inline constexpr unsigned int surface_lowest = 0x0b0e12;
inline constexpr unsigned int surface_low = 0x141921;
inline constexpr unsigned int surface_container = 0x1a2028;
inline constexpr unsigned int surface_high = 0x1e252e;
inline constexpr unsigned int surface_highest = 0x242c36;
inline constexpr unsigned int outline = 0x3c4652;
inline constexpr unsigned int outline_variant = 0x2b333d;
inline constexpr unsigned int on_surface = 0xe3e9ef;
inline constexpr unsigned int on_surface_variant = 0xa9b5c1;
inline constexpr unsigned int on_surface_muted = 0x7c8794;
inline constexpr unsigned int primary = 0x9ec6e2;
inline constexpr unsigned int primary_hover = 0xb6d6eb;
inline constexpr unsigned int primary_pressed = 0x8ab6d2;
inline constexpr unsigned int on_primary = 0x0b141a;
inline constexpr unsigned int primary_container = 0x1f3848;
inline constexpr unsigned int on_primary_container = 0xcfe8f8;
inline constexpr unsigned int tertiary = 0xd8c08c;
inline constexpr unsigned int error = 0xf2b8b5;
inline constexpr unsigned int success = 0x9ccfa8;
} // namespace colors

class NordicTheme {
public:
    static QPalette palette() {
        QPalette palette;
        palette.setColor(QPalette::Window, QColor(colors::surface_low));
        palette.setColor(QPalette::WindowText, QColor(colors::on_surface));
        palette.setColor(QPalette::Base, QColor(0x0e1217));
        palette.setColor(QPalette::AlternateBase, QColor(0x121820));
        palette.setColor(QPalette::Text, QColor(colors::on_surface));
        palette.setColor(QPalette::Button, QColor(colors::surface_high));
        palette.setColor(QPalette::ButtonText, QColor(colors::on_surface));
        palette.setColor(QPalette::BrightText, QColor(0xffffff));
        palette.setColor(QPalette::Highlight, QColor(0x3e6a85));
        palette.setColor(QPalette::HighlightedText, QColor(0xe9f3fa));
        palette.setColor(QPalette::ToolTipBase, QColor(colors::surface_highest));
        palette.setColor(QPalette::ToolTipText, QColor(colors::on_surface));
        palette.setColor(QPalette::PlaceholderText, QColor(colors::on_surface_muted));
        palette.setColor(QPalette::Link, QColor(colors::primary));
        palette.setColor(QPalette::Mid, QColor(colors::outline_variant));
        palette.setColor(QPalette::Midlight, QColor(colors::outline));
        palette.setColor(QPalette::Dark, QColor(colors::surface_lowest));
        palette.setColor(QPalette::Light, QColor(0x4a5563));
        for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
            palette.setColor(QPalette::Disabled, role, QColor(0x5e6773));
        }
        palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x2a323a));
        return palette;
    }

    static QString style_sheet() {
        return QStringLiteral(R"(
QWidget {
    color: #e3e9ef;
    font-family: "Segoe UI Variable Text", "Segoe UI", "Yu Gothic UI", sans-serif;
    font-size: 13px;
}
QMainWindow, QDialog { background: #101419; }
QMainWindow::separator {
    background: #0b0e12;
    width: 4px;
    height: 4px;
}
QMainWindow::separator:hover { background: #24303c; }

QMenuBar {
    background: #101419;
    border-bottom: 1px solid #2b333d;
    padding: 2px 6px;
}
QMenuBar::item {
    background: transparent;
    border-radius: 6px;
    padding: 5px 10px;
}
QMenuBar::item:selected { background: #1e252e; }
QMenuBar::item:pressed { background: #24303c; }

QMenu {
    background: #181e26;
    border: 1px solid #2b333d;
    border-radius: 10px;
    padding: 6px;
}
QMenu::item {
    border-radius: 6px;
    padding: 6px 28px 6px 12px;
}
QMenu::item:selected { background: #24303c; }
QMenu::item:disabled { color: #5e6773; }
QMenu::separator {
    height: 1px;
    background: #2b333d;
    margin: 6px 10px;
}
QMenu::indicator { width: 14px; height: 14px; }

QDockWidget {
    background: #141921;
    color: #e3e9ef;
    titlebar-close-icon: url(:/icons/close.svg);
    titlebar-normal-icon: url(:/icons/float.svg);
}
QDockWidget::title {
    background: #1a2028;
    border-bottom: 1px solid #2b333d;
    padding: 7px 4px 7px 10px;
    text-align: left;
    font-weight: 600;
}
QDockWidget::close-button, QDockWidget::float-button {
    background: transparent;
    border: none;
    border-radius: 4px;
    width: 16px;
    height: 16px;
    margin: 4px;
    padding: 2px;
}
QDockWidget::close-button { image: url(:/icons/close.svg); }
QDockWidget::float-button { image: url(:/icons/float.svg); }
QDockWidget::close-button:hover, QDockWidget::float-button:hover { background: #2b333d; }
QDockWidget::close-button:pressed, QDockWidget::float-button:pressed { background: #24303c; }

QTabBar { background: #1a2028; }
QToolButton#TabCloseButton {
    min-width: 16px;
    max-width: 16px;
    min-height: 16px;
    max-height: 16px;
    padding: 0;
    margin: 0;
    border: none;
    border-radius: 3px;
    background: transparent;
}
QToolButton#TabCloseButton:hover { background: #2b333d; }
QToolButton#TabCloseButton:pressed { background: #24303c; }
QTabBar::tab {
    background: transparent;
    color: #a9b5c1;
    border: none;
    border-bottom: 2px solid transparent;
    padding: 8px 26px 8px 10px;
    margin-right: 2px;
    font-size: 11px;
    font-weight: 500;
}
QTabBar::tab:hover { color: #e3e9ef; background: #1e252e; }
QTabBar::tab:selected {
    color: #cfe8f8;
    border-bottom-color: #9ec6e2;
    background: transparent;
}
QTabBar::scroller { width: 20px; }
QTabBar QToolButton {
    background: transparent;
    border: none;
    border-radius: 4px;
}
QTabBar QToolButton:hover { background: #2b333d; }

QPushButton {
    background: #1e252e;
    color: #e3e9ef;
    border: 1px solid #2b333d;
    border-radius: 8px;
    padding: 0 12px;
    min-height: 30px;
    font-weight: 500;
}
QPushButton:hover { background: #242c36; border-color: #3c4652; }
QPushButton:pressed { background: #182029; }
QPushButton:focus { border-color: #6e9ebc; }
QPushButton:checked {
    background: #1f3848;
    border-color: #3e6a85;
    color: #cfe8f8;
}
QPushButton:disabled {
    background: #151a21;
    border-color: #232a32;
    color: #5e6773;
}
QPushButton#PrimaryButton {
    background: #9ec6e2;
    border: none;
    color: #0b141a;
    font-weight: 600;
}
QPushButton#PrimaryButton:hover { background: #b6d6eb; }
QPushButton#PrimaryButton:pressed { background: #8ab6d2; }
QPushButton#DangerButton { color: #f2b8b5; }

QToolButton {
    background: transparent;
    color: #a9b5c1;
    border: none;
    border-radius: 8px;
    padding: 4px;
    min-width: 28px;
    min-height: 28px;
}
QToolButton:hover { background: #242c36; color: #e3e9ef; }
QToolButton:pressed { background: #182029; }
QToolButton:checked { background: #1f3848; color: #cfe8f8; }
QToolButton:focus { border: 1px solid #6e9ebc; }
QToolButton#TransportButton { min-width: 32px; min-height: 32px; }
QToolButton#TransportButton:disabled { color: #5e6773; }

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background: #0e1217;
    color: #e3e9ef;
    border: 1px solid #2b333d;
    border-radius: 8px;
    padding: 0 8px;
    min-height: 30px;
    selection-background-color: #3e6a85;
    selection-color: #e9f3fa;
}
QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover, QComboBox:hover {
    border-color: #3c4652;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
    border-color: #6e9ebc;
}
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled {
    background: #12161b;
    border-color: #232a32;
    color: #5e6773;
}
QSpinBox, QDoubleSpinBox {
    font-family: "Cascadia Mono", "Consolas", monospace;
    font-size: 12px;
}
QSpinBox::up-button, QDoubleSpinBox::up-button {
    subcontrol-origin: border;
    subcontrol-position: top right;
    width: 20px;
    border: none;
    border-left: 1px solid #2b333d;
    border-top-right-radius: 8px;
    background: transparent;
}
QSpinBox::down-button, QDoubleSpinBox::down-button {
    subcontrol-origin: border;
    subcontrol-position: bottom right;
    width: 20px;
    border: none;
    border-left: 1px solid #2b333d;
    border-bottom-right-radius: 8px;
    background: transparent;
}
QSpinBox::up-button:hover, QDoubleSpinBox::up-button:hover,
QSpinBox::down-button:hover, QDoubleSpinBox::down-button:hover {
    background: #1e252e;
}
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {
    image: url(:/icons/chevron-up.svg);
    width: 10px;
    height: 10px;
}
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {
    image: url(:/icons/chevron-down.svg);
    width: 10px;
    height: 10px;
}
QComboBox::drop-down {
    width: 24px;
    border: none;
    background: transparent;
}
QComboBox::down-arrow {
    image: url(:/icons/chevron-down.svg);
    width: 12px;
    height: 12px;
}
QComboBox QAbstractItemView {
    background: #181e26;
    border: 1px solid #2b333d;
    border-radius: 8px;
    padding: 4px;
    selection-background-color: #24303c;
    selection-color: #e3e9ef;
    outline: none;
}
QComboBox QAbstractItemView::item {
    min-height: 28px;
    padding: 0 8px;
    border-radius: 6px;
}

QCheckBox, QRadioButton { spacing: 8px; }
QCheckBox::indicator, QRadioButton::indicator { width: 16px; height: 16px; }
QCheckBox::indicator:unchecked {
    border: 1px solid #4a5563;
    background: #0e1217;
    border-radius: 4px;
}
QCheckBox::indicator:unchecked:hover { border-color: #6e9ebc; }
QCheckBox::indicator:checked {
    border: 1px solid #9ec6e2;
    background: #9ec6e2;
    border-radius: 4px;
    image: url(:/icons/check.svg);
}
QCheckBox::indicator:checked:hover { background: #b6d6eb; }
QCheckBox::indicator:disabled { border-color: #2b333d; background: #12161b; }
QRadioButton::indicator:unchecked {
    border: 1px solid #4a5563;
    background: #0e1217;
    border-radius: 8px;
}
QRadioButton::indicator:checked {
    border: 1px solid #9ec6e2;
    background: #9ec6e2;
    border-radius: 8px;
    image: url(:/icons/dot.svg);
}
QCheckBox:disabled, QRadioButton:disabled { color: #5e6773; }

QSlider::groove:horizontal {
    height: 4px;
    background: #2a323a;
    border-radius: 2px;
}
QSlider::sub-page:horizontal {
    background: #9ec6e2;
    border-radius: 2px;
}
QSlider::handle:horizontal {
    width: 14px;
    height: 14px;
    margin: -5px 0;
    background: #cfe8f8;
    border: 1px solid #0b0e12;
    border-radius: 7px;
}
QSlider::handle:horizontal:hover { background: #ffffff; }
QSlider::handle:horizontal:disabled { background: #3c4652; }

QListWidget, QTreeWidget, QTableWidget, QTreeView, QListView {
    background: #0e1217;
    color: #e3e9ef;
    border: 1px solid #2b333d;
    border-radius: 10px;
    alternate-background-color: #121820;
    selection-background-color: #1f3848;
    selection-color: #cfe8f8;
    outline: none;
}
QTreeView::item, QListView::item { min-height: 28px; padding: 0 8px; }

QScrollArea { border: none; background: transparent; }
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 2px;
}
QScrollBar::handle:vertical {
    background: #3c4652;
    min-height: 24px;
    border-radius: 5px;
}
QScrollBar::handle:vertical:hover { background: #5a6876; }
QScrollBar:horizontal {
    background: transparent;
    height: 10px;
    margin: 2px;
}
QScrollBar::handle:horizontal {
    background: #3c4652;
    min-width: 24px;
    border-radius: 5px;
}
QScrollBar::handle:horizontal:hover { background: #5a6876; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QStatusBar {
    background: #0b0e12;
    border-top: 1px solid #232a32;
}
QStatusBar::item { border: none; }
QStatusBar QLabel { background: transparent; }

QGroupBox {
    border: 1px solid #2b333d;
    border-radius: 12px;
    margin-top: 10px;
    padding-top: 10px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 4px;
    color: #a9b5c1;
}

QLabel#SectionLabel {
    color: #a9b5c1;
    font-size: 12px;
    font-weight: 600;
}
QLabel#HintLabel {
    color: #7c8794;
    font-size: 12px;
}
QLabel#FieldLabel {
    color: #a9b5c1;
    font-size: 12px;
}
QLabel#ViewportTitle {
    color: #e3e9ef;
    font-size: 13px;
    font-weight: 600;
    background: transparent;
}
QLabel#ViewportCaption {
    color: #7c8794;
    font-size: 11px;
    background: transparent;
}
QLabel#StatsLabel {
    color: #a9b5c1;
    font-family: "Cascadia Mono", "Consolas", monospace;
    font-size: 12px;
}
QLabel#StatusLabel {
    color: #a9b5c1;
    font-size: 12px;
}

QScrollArea#PanelScroll, QWidget#PanelBody { background: #141921; }
QWidget#ViewportArea { background: #0b0e12; }
QWidget#ViewportHeader, QWidget#ViewportFooter { background: #141921; }
QWidget#ViewportHeader { border-bottom: 1px solid #2b333d; }
QWidget#ViewportFooter { border-top: 1px solid #2b333d; }
QWidget#ToolbarDivider { background: #2b333d; }
QPushButton#ViewportChip {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 8px;
    padding: 0 10px;
    min-height: 26px;
    color: #a9b5c1;
    font-weight: 500;
}
QPushButton#ViewportChip:hover { background: #1e252e; color: #e3e9ef; }
QPushButton#ViewportChip:checked {
    background: #1f3848;
    border-color: #3e6a85;
    color: #cfe8f8;
}
QPushButton#ViewportMode {
    background: #1f3848;
    border: 1px solid #3e6a85;
    border-radius: 8px;
    padding: 0 12px;
    color: #cfe8f8;
    font-weight: 600;
}
QPushButton#ViewportMode:hover { background: #27465a; }
QDoubleSpinBox#ViewportField, QSpinBox#ViewportField {
    background: #0e1217;
    border: 1px solid #2b333d;
    border-radius: 8px;
    padding: 0 6px;
    min-height: 28px;
    max-height: 28px;
    font-size: 12px;
}

QToolTip {
    background: #242c36;
    color: #e3e9ef;
    border: 1px solid #3c4652;
    border-radius: 6px;
    padding: 6px 8px;
}
)");
    }
};

} // namespace hikari::studio
