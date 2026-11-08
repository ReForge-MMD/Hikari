#include "hikari/studio/localization.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QGroupBox>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>

#include <utility>

#include <functional>

namespace hikari::studio::localization {
namespace {

struct Entry {
    QString ru;
    QString ja;
};

constexpr const char* kSkip = "hikari_i18n_skip";
constexpr const char* kText = "hikari_i18n_text";
constexpr const char* kToolTip = "hikari_i18n_tooltip";
constexpr const char* kStatusTip = "hikari_i18n_statustip";
constexpr const char* kItems = "hikari_i18n_items";
constexpr const char* kTabs = "hikari_i18n_tabs";
constexpr const char* kSetting = "uiLanguage";

Language g_language = Language::English;

const QHash<QString, Entry>& dictionary() {
    static const QHash<QString, Entry> entries = {
        {QStringLiteral("&File"), {QStringLiteral("&Файл"), QStringLiteral("&ファイル")}},
        {QStringLiteral("&Edit"), {QStringLiteral("&Правка"), QStringLiteral("&編集")}},
        {QStringLiteral("&View"), {QStringLiteral("&Вид"), QStringLiteral("&表示")}},
        {QStringLiteral("&Facial"), {QStringLiteral("&Лицо"), QStringLiteral("&表情")}},
        {QStringLiteral("&Physics"), {QStringLiteral("&Физика"), QStringLiteral("&物理")}},
        {QStringLiteral("&Help"), {QStringLiteral("&Справка"), QStringLiteral("&ヘルプ")}},
        {QStringLiteral("&Window"), {QStringLiteral("&Окно"), QStringLiteral("&ウィンドウ")}},
        {QStringLiteral("New Project"), {QStringLiteral("Новый проект"), QStringLiteral("新規プロジェクト")}},
        {QStringLiteral("Open Project (.hkp)..."), {QStringLiteral("Открыть проект (.hkp)..."), QStringLiteral("プロジェクトを開く (.hkp)...")}},
        {QStringLiteral("Save Project (.hkp)..."), {QStringLiteral("Сохранить проект (.hkp)..."), QStringLiteral("プロジェクトを保存 (.hkp)...")}},
        {QStringLiteral("Load PMX Model..."), {QStringLiteral("Загрузить модель PMX..."), QStringLiteral("PMXモデルを読み込み...")}},
        {QStringLiteral("Load PMX..."), {QStringLiteral("Загрузить PMX..."), QStringLiteral("PMXを読み込み...")}},
        {QStringLiteral("Load Motion (.VMD)..."), {QStringLiteral("Загрузить движение (.VMD)..."), QStringLiteral("モーションを読み込み (.VMD)...")}},
        {QStringLiteral("Load Audio..."), {QStringLiteral("Загрузить аудио..."), QStringLiteral("オーディオを読み込み...")}},
        {QStringLiteral("Unload Audio"), {QStringLiteral("Выгрузить аудио"), QStringLiteral("オーディオを解除")}},
        {QStringLiteral("Load Hikari Motion (.hkm)..."), {QStringLiteral("Загрузить движение Hikari (.hkm)..."), QStringLiteral("Hikariモーションを読み込み (.hkm)...")}},
        {QStringLiteral("Load Pose (.VPD)..."), {QStringLiteral("Загрузить позу (.VPD)..."), QStringLiteral("ポーズを読み込み (.VPD)...")}},
        {QStringLiteral("Save Pose (.VPD)..."), {QStringLiteral("Сохранить позу (.VPD)..."), QStringLiteral("ポーズを保存 (.VPD)...")}},
        {QStringLiteral("Save Motion (.VMD)..."), {QStringLiteral("Сохранить движение (.VMD)..."), QStringLiteral("モーションを保存 (.VMD)...")}},
        {QStringLiteral("Save Hikari Motion (.hkm)..."), {QStringLiteral("Сохранить движение Hikari (.hkm)..."), QStringLiteral("Hikariモーションを保存 (.hkm)...")}},
        {QStringLiteral("Render Image (BMP)..."), {QStringLiteral("Сохранить изображение (BMP)..."), QStringLiteral("画像を出力 (BMP)...")}},
        {QStringLiteral("Render Frame Sequence..."), {QStringLiteral("Рендер последовательности кадров..."), QStringLiteral("連番フレームを出力...")}},
        {QStringLiteral("Set Working Directory..."), {QStringLiteral("Задать рабочий каталог..."), QStringLiteral("作業ディレクトリを設定...")}},
        {QStringLiteral("Exit"), {QStringLiteral("Выход"), QStringLiteral("終了")}},
        {QStringLiteral("Undo"), {QStringLiteral("Отменить"), QStringLiteral("元に戻す")}},
        {QStringLiteral("Redo"), {QStringLiteral("Повторить"), QStringLiteral("やり直す")}},
        {QStringLiteral("Reset Bone Transform (Z)"), {QStringLiteral("Сбросить трансформ кости (Z)"), QStringLiteral("ボーントランスフォームをリセット (Z)")}},
        {QStringLiteral("Delete Model"), {QStringLiteral("Удалить модель"), QStringLiteral("モデルを削除")}},
        {QStringLiteral("Select All Bone Frames (N)"), {QStringLiteral("Выбрать все кадры костей (N)"), QStringLiteral("全ボーンフレームを選択 (N)")}},
        {QStringLiteral("Delete Frame Line (K)"), {QStringLiteral("Удалить линию кадра (K)"), QStringLiteral("フレーム行を削除 (K)")}},
        {QStringLiteral("Interpolation Curve Editor..."), {QStringLiteral("Редактор кривой интерполяции..."), QStringLiteral("補間カーブエディタ...")}},
        {QStringLiteral("Coordinate Grid (G)"), {QStringLiteral("Координатная сетка (G)"), QStringLiteral("座標グリッド (G)")}},
        {QStringLiteral("Skeleton Overlay"), {QStringLiteral("Оверлей скелета"), QStringLiteral("スケルトン表示")}},
        {QStringLiteral("Skeleton On All Models"), {QStringLiteral("Скелет на всех моделях"), QStringLiteral("全モデルのスケルトン")}},
        {QStringLiteral("Physics Bone Overlay"), {QStringLiteral("Оверлей физических костей"), QStringLiteral("物理ボーン表示")}},
        {QStringLiteral("Ground Shadow"), {QStringLiteral("Тень на полу"), QStringLiteral("地面影")}},
        {QStringLiteral("MMD White Canvas"), {QStringLiteral("Белый фон MMD"), QStringLiteral("MMD白背景")}},
        {QStringLiteral("Wireframe (R)"), {QStringLiteral("Каркас (R)"), QStringLiteral("ワイヤーフレーム (R)")}},
        {QStringLiteral("Reset View"), {QStringLiteral("Сбросить вид"), QStringLiteral("視点をリセット")}},
        {QStringLiteral("Reset view"), {QStringLiteral("Сбросить вид"), QStringLiteral("視点をリセット")}},
        {QStringLiteral("camera"), {QStringLiteral("Камера"), QStringLiteral("カメラ")}},
        {QStringLiteral("light"), {QStringLiteral("Свет"), QStringLiteral("ライト")}},
        {QStringLiteral("shadow"), {QStringLiteral("Тень"), QStringLiteral("影")}},
        {QStringLiteral("gravity"), {QStringLiteral("Гравитация"), QStringLiteral("重力")}},
        {QStringLiteral("accessory"), {QStringLiteral("Аксессуар"), QStringLiteral("アクセサリ")}},
        {QStringLiteral("view cnt"), {QStringLiteral("Счётчик вида"), QStringLiteral("表示数")}},
        {QStringLiteral("model scale"), {QStringLiteral("Трансформ модели"), QStringLiteral("モデル変形")}},
        {QStringLiteral("Screen Size..."), {QStringLiteral("Размер экрана..."), QStringLiteral("画面サイズ...")}},
        {QStringLiteral("Fullscreen"), {QStringLiteral("Полный экран"), QStringLiteral("全画面")}},
        {QStringLiteral("Panels"), {QStringLiteral("Панели"), QStringLiteral("パネル")}},
        {QStringLiteral("Reset Panel Layout"), {QStringLiteral("Сбросить раскладку панелей"), QStringLiteral("パネル配置をリセット")}},
        {QStringLiteral("Reset All Facial Values"), {QStringLiteral("Сбросить все значения лица"), QStringLiteral("表情値をすべてリセット")}},
        {QStringLiteral("Register All Facial Frames"), {QStringLiteral("Записать все кадры лица"), QStringLiteral("全表情フレームを登録")}},
        {QStringLiteral("Simulate Only While Playing"), {QStringLiteral("Симулировать только при воспроизведении"), QStringLiteral("再生中のみシミュレーション")}},
        {QStringLiteral("Ground Collision"), {QStringLiteral("Столкновение с полом"), QStringLiteral("地面衝突")}},
        {QStringLiteral("Simulation Speed"), {QStringLiteral("Скорость симуляции"), QStringLiteral("シミュレーション速度")}},
        {QStringLiteral("Gravity Settings..."), {QStringLiteral("Настройки гравитации..."), QStringLiteral("重力設定...")}},
        {QStringLiteral("Reset Physics"), {QStringLiteral("Сбросить физику"), QStringLiteral("物理をリセット")}},
        {QStringLiteral("Show Rigid Bodies"), {QStringLiteral("Показать твёрдые тела"), QStringLiteral("剛体を表示")}},
        {QStringLiteral("Show Joints"), {QStringLiteral("Показать шарниры"), QStringLiteral("ジョイントを表示")}},
        {QStringLiteral("About Jolt Physics"), {QStringLiteral("О Jolt Physics"), QStringLiteral("Jolt Physicsについて")}},
        {QStringLiteral("Keyboard Shortcuts"), {QStringLiteral("Горячие клавиши"), QStringLiteral("キーボードショートカット")}},
        {QStringLiteral("About Hikari Studio"), {QStringLiteral("О Hikari Studio"), QStringLiteral("Hikari Studioについて")}},
        {QStringLiteral("Language"), {QStringLiteral("Язык"), QStringLiteral("言語")}},
        {QStringLiteral("Interface language: %1"), {QStringLiteral("Язык интерфейса: %1"), QStringLiteral("表示言語: %1")}},
        {QStringLiteral("English"), {QStringLiteral("Английский"), QStringLiteral("英語")}},
        {QStringLiteral("Russian"), {QStringLiteral("Русский"), QStringLiteral("ロシア語")}},
        {QStringLiteral("Japanese"), {QStringLiteral("Японский"), QStringLiteral("日本語")}},
        {QStringLiteral("Model"), {QStringLiteral("Модель"), QStringLiteral("モデル")}},
        {QStringLiteral("Bone"), {QStringLiteral("Кость"), QStringLiteral("ボーン")}},
        {QStringLiteral("Facial"), {QStringLiteral("Лицо"), QStringLiteral("表情")}},
        {QStringLiteral("Camera"), {QStringLiteral("Камера"), QStringLiteral("カメラ")}},
        {QStringLiteral("Light"), {QStringLiteral("Свет"), QStringLiteral("ライト")}},
        {QStringLiteral("Shadow"), {QStringLiteral("Тень"), QStringLiteral("影")}},
        {QStringLiteral("Accessory"), {QStringLiteral("Аксессуар"), QStringLiteral("アクセサリ")}},
        {QStringLiteral("View"), {QStringLiteral("Вид"), QStringLiteral("表示")}},
        {QStringLiteral("Playback"), {QStringLiteral("Воспроизведение"), QStringLiteral("再生")}},
        {QStringLiteral("Interpolation"), {QStringLiteral("Интерполяция"), QStringLiteral("補間")}},
        {QStringLiteral("Timeline"), {QStringLiteral("Таймлайн"), QStringLiteral("タイムライン")}},
        {QStringLiteral("Active model. Editing applies to this model."), {QStringLiteral("Активная модель. Редактирование применяется к ней."), QStringLiteral("アクティブモデル。編集はこのモデルに適用されます。")}},
        {QStringLiteral("Load PMX..."), {QStringLiteral("Загрузить PMX..."), QStringLiteral("PMXを読み込み...")}},
        {QStringLiteral("Remove"), {QStringLiteral("Удалить"), QStringLiteral("削除")}},
        {QStringLiteral("Display"), {QStringLiteral("Отображение"), QStringLiteral("表示")}},
        {QStringLiteral("Scale"), {QStringLiteral("Масштаб"), QStringLiteral("スケール")}},
        {QStringLiteral("Uniform model scale. Poses, motions and physics keep their authored model-space units and scale together with the model."), {QStringLiteral("Равномерный масштаб модели. Позы, движения и физика сохраняют авторские единицы и масштабируются вместе с моделью."), QStringLiteral("モデルの均一スケール。ポーズ・モーション・物理は作者のモデル空間単位を保ち、モデルと一緒に拡大縮小されます。")}},
        {QStringLiteral("Key"), {QStringLiteral("Ключ"), QStringLiteral("キー")}},
        {QStringLiteral("Key transform"), {QStringLiteral("Ключ трансформа"), QStringLiteral("トランスフォームをキー")}},
        {QStringLiteral("Register the model scale at the current frame"), {QStringLiteral("Записать масштаб модели на текущем кадре"), QStringLiteral("現在のフレームにモデルのスケールを登録")}},
        {QStringLiteral("Register model position, rotation, scale and visibility at the current frame"), {QStringLiteral("Записать позицию, поворот, масштаб и видимость модели на текущем кадре"), QStringLiteral("現在のフレームにモデルの位置・回転・スケール・表示を登録")}},
        {QStringLiteral("Register Disp/IK/OP"), {QStringLiteral("Записать Disp/IK/OP"), QStringLiteral("Disp/IK/OPを登録")}},
        {QStringLiteral("Register IK on/off state and outside-parent registrations at the current frame. Bone display is not stored in VMD."), {QStringLiteral("Записать состояние IK и внешних родителей на текущем кадре. Отображение костей в VMD не сохраняется."), QStringLiteral("現在のフレームにIKのオン/オフと外部親を登録します。ボーン表示はVMDに保存されません。")}},
        {QStringLiteral("Mode"), {QStringLiteral("Режим"), QStringLiteral("モード")}},
        {QStringLiteral("Select"), {QStringLiteral("Выбор"), QStringLiteral("選択")}},
        {QStringLiteral("Rotate"), {QStringLiteral("Вращение"), QStringLiteral("回転")}},
        {QStringLiteral("Move"), {QStringLiteral("Перемещение"), QStringLiteral("移動")}},
        {QStringLiteral("Bones"), {QStringLiteral("Кости"), QStringLiteral("ボーン")}},
        {QStringLiteral("Physics"), {QStringLiteral("Физика"), QStringLiteral("物理")}},
        {QStringLiteral("IK"), {QStringLiteral("IK"), QStringLiteral("IK")}},
        {QStringLiteral("Enable or disable IK for the selected IK controller bone."), {QStringLiteral("Включить или отключить IK для выбранной кости управления IK."), QStringLiteral("選択したIK制御ボーンのIKを有効/無効にします。")}},
        {QStringLiteral("Move %1"), {QStringLiteral("Смещение %1"), QStringLiteral("移動 %1")}},
        {QStringLiteral("Angle %1"), {QStringLiteral("Угол %1"), QStringLiteral("角度 %1")}},
        {QStringLiteral("Reset"), {QStringLiteral("Сброс"), QStringLiteral("リセット")}},
        {QStringLiteral("Register"), {QStringLiteral("Записать"), QStringLiteral("登録")}},
        {QStringLiteral("OP Register (Outside Parent)..."), {QStringLiteral("Регистрация OP (внешний родитель)..."), QStringLiteral("OP登録 (外部親)...")}},
        {QStringLiteral("Attach a control/IK bone or the whole model to a bone of another model. The registration is stored in the Disp/IK/OP track."), {QStringLiteral("Прикрепить кость управления/IK или всю модель к кости другой модели. Регистрация хранится в дорожке Disp/IK/OP."), QStringLiteral("制御/IKボーンまたはモデル全体を別モデルのボーンに接続します。登録はDisp/IK/OPトラックに保存されます。")}},
        {QStringLiteral("Eyes"), {QStringLiteral("Глаза"), QStringLiteral("目")}},
        {QStringLiteral("Mouth"), {QStringLiteral("Рот"), QStringLiteral("口")}},
        {QStringLiteral("Brow"), {QStringLiteral("Брови"), QStringLiteral("眉")}},
        {QStringLiteral("Other"), {QStringLiteral("Прочее"), QStringLiteral("その他")}},
        {QStringLiteral("Distance"), {QStringLiteral("Дистанция"), QStringLiteral("距離")}},
        {QStringLiteral("FOV"), {QStringLiteral("FOV"), QStringLiteral("FOV")}},
        {QStringLiteral("Yaw"), {QStringLiteral("Рыскание"), QStringLiteral("ヨー")}},
        {QStringLiteral("Pitch"), {QStringLiteral("Наклон"), QStringLiteral("ピッチ")}},
        {QStringLiteral("Roll"), {QStringLiteral("Крен"), QStringLiteral("ロール")}},
        {QStringLiteral("Target"), {QStringLiteral("Цель"), QStringLiteral("ターゲット")}},
        {QStringLiteral("Free"), {QStringLiteral("Свободный"), QStringLiteral("フリー")}},
        {QStringLiteral("Selected bone"), {QStringLiteral("Выбранная кость"), QStringLiteral("選択ボーン")}},
        {QStringLiteral("Camera rotation pivot in model mode: free point, model center, or the selected bone."), {QStringLiteral("Точка вращения камеры в режиме модели: свободная точка, центр модели или выбранная кость."), QStringLiteral("モデルモードでのカメラ回転中心: フリーポイント、モデル中心、選択ボーン。")}},
        {QStringLiteral("Near clip"), {QStringLiteral("Ближняя плоскость"), QStringLiteral("近クリップ")}},
        {QStringLiteral("Far clip"), {QStringLiteral("Дальняя плоскость"), QStringLiteral("遠クリップ")}},
        {QStringLiteral("Camera distance. Positive moves the eye away from the focus, negative crosses the pivot; VMD files store the opposite sign."), {QStringLiteral("Дистанция камеры. Положительная отодвигает глаз от фокуса, отрицательная пересекает пивот; в VMD хранится обратный знак."), QStringLiteral("カメラ距離。正の値はフォーカスから遠ざかり、負の値はピボットを越えます。VMDでは逆符号で保存されます。")}},
        {QStringLiteral("Near clipping plane. Raise it to cut away geometry around a POV camera, for example the head it sits inside."), {QStringLiteral("Ближняя плоскость отсечения. Увеличьте, чтобы убрать геометрию вокруг камеры от первого лица, например голову."), QStringLiteral("近クリップ面。上げるとPOVカメラ周辺のジオメトリ（頭など）を切り取ります。")}},
        {QStringLiteral("Far clipping plane."), {QStringLiteral("Дальняя плоскость отсечения."), QStringLiteral("遠クリップ面。")}},
        {QStringLiteral("Follow bone (Track)"), {QStringLiteral("Следовать за костью (Track)"), QStringLiteral("ボーン追従 (トラック)")}},
        {QStringLiteral("MMD camera tracking: ride the selected model's bone and inherit its world rotation. The parent pair is stored in camera keyframes."), {QStringLiteral("Трекинг камеры MMD: следовать за костью выбранной модели и наследовать её мировой поворот. Пара родителя хранится в камерных ключах."), QStringLiteral("MMDカメラトラック: 選択モデルのボーンに追従し、そのワールド回転を継承します。親ペアはカメラキーに保存されます。")}},
        {QStringLiteral("Offset"), {QStringLiteral("Смещение"), QStringLiteral("オフセット")}},
        {QStringLiteral("Camera offset in the followed bone's local frame."), {QStringLiteral("Смещение камеры в локальной системе отслеживаемой кости."), QStringLiteral("追従ボーンのローカル座標系でのカメラオフセット。")}},
        {QStringLiteral("Reset Camera"), {QStringLiteral("Сбросить камеру"), QStringLiteral("カメラをリセット")}},
        {QStringLiteral("Ortho"), {QStringLiteral("Орто"), QStringLiteral("平行投影")}},
        {QStringLiteral("Preset"), {QStringLiteral("Пресет"), QStringLiteral("プリセット")}},
        {QStringLiteral("Save"), {QStringLiteral("Сохранить"), QStringLiteral("保存")}},
        {QStringLiteral("Load"), {QStringLiteral("Загрузить"), QStringLiteral("読み込み")}},
        {QStringLiteral("Delete"), {QStringLiteral("Удалить"), QStringLiteral("削除")}},
        {QStringLiteral("Register Camera Keyframe"), {QStringLiteral("Записать ключ камеры"), QStringLiteral("カメラキーを登録")}},
        {QStringLiteral("Camera target: free"), {QStringLiteral("Цель камеры: свободная"), QStringLiteral("カメラターゲット: フリー")}},
        {QStringLiteral("Camera target: model"), {QStringLiteral("Цель камеры: модель"), QStringLiteral("カメラターゲット: モデル")}},
        {QStringLiteral("Camera target: selected bone"), {QStringLiteral("Цель камеры: выбранная кость"), QStringLiteral("カメラターゲット: 選択ボーン")}},
        {QStringLiteral("Camera follow bone enabled"), {QStringLiteral("Следование камеры за костью включено"), QStringLiteral("カメラのボーン追従を有効にしました")}},
        {QStringLiteral("Camera follow bone disabled"), {QStringLiteral("Следование камеры за костью отключено"), QStringLiteral("カメラのボーン追従を無効にしました")}},
        {QStringLiteral("Camera view reset"), {QStringLiteral("Вид камеры сброшен"), QStringLiteral("カメラ視点をリセットしました")}},
        {QStringLiteral("Loaded camera preset: %1"), {QStringLiteral("Загружен пресет камеры: %1"), QStringLiteral("カメラプリセットを読み込みました: %1")}},
        {QStringLiteral("Save camera preset"), {QStringLiteral("Сохранить пресет камеры"), QStringLiteral("カメラプリセットを保存")}},
        {QStringLiteral("Preset name:"), {QStringLiteral("Имя пресета:"), QStringLiteral("プリセット名:")}},
        {QStringLiteral("R"), {QStringLiteral("R"), QStringLiteral("R")}},
        {QStringLiteral("G"), {QStringLiteral("G"), QStringLiteral("G")}},
        {QStringLiteral("B"), {QStringLiteral("B"), QStringLiteral("B")}},
        {QStringLiteral("Dir %1"), {QStringLiteral("Напр. %1"), QStringLiteral("方向 %1")}},
        {QStringLiteral("Register Shadow Keyframe"), {QStringLiteral("Записать ключ тени"), QStringLiteral("影キーを登録")}},
        {QStringLiteral("Off"), {QStringLiteral("Выкл."), QStringLiteral("オフ")}},
        {QStringLiteral("None"), {QStringLiteral("Нет"), QStringLiteral("なし")}},
        {QStringLiteral("none"), {QStringLiteral("нет"), QStringLiteral("なし")}},
        {QStringLiteral("Mode 1"), {QStringLiteral("Режим 1"), QStringLiteral("モード1")}},
        {QStringLiteral("Mode 2"), {QStringLiteral("Режим 2"), QStringLiteral("モード2")}},
        {QStringLiteral("Range"), {QStringLiteral("Диапазон"), QStringLiteral("範囲")}},
        {QStringLiteral("Detail"), {QStringLiteral("Детализация"), QStringLiteral("詳細")}},
        {QStringLiteral("Self-shadow mapping is not rendered in this pass. These values are authored and exported to VMD self-shadow frames."), {QStringLiteral("Самозатенение пока не отображается. Значения записываются и экспортируются в кадры самозатенения VMD."), QStringLiteral("セルフシャドウはこのパスでは描画されません。値はVMDのセルフシャドウフレームに保存・出力されます。")}},
        {QStringLiteral("Load .x..."), {QStringLiteral("Загрузить .x..."), QStringLiteral(" .xを読み込み...")}},
        {QStringLiteral("Accessory rendering is not implemented in this pass."), {QStringLiteral("Рендер аксессуаров в этом проходе не реализован."), QStringLiteral("アクセサリ描画はこのパスでは実装されていません。")}},
        {QStringLiteral("Alpha"), {QStringLiteral("Альфа"), QStringLiteral("アルファ")}},
        {QStringLiteral("Register Accessory Keyframe"), {QStringLiteral("Записать ключ аксессуара"), QStringLiteral("アクセサリキーを登録")}},
        {QStringLiteral("Ground grid"), {QStringLiteral("Сетка пола"), QStringLiteral("地面グリッド")}},
        {QStringLiteral("White canvas"), {QStringLiteral("Белый фон"), QStringLiteral("白背景")}},
        {QStringLiteral("Wireframe"), {QStringLiteral("Каркас"), QStringLiteral("ワイヤーフレーム")}},
        {QStringLiteral("Information display"), {QStringLiteral("Информация"), QStringLiteral("情報表示")}},
        {QStringLiteral("Save CPU while idle"), {QStringLiteral("Экономить CPU в простое"), QStringLiteral("アイドル時のCPU節約")}},
        {QStringLiteral("Fullscreen render"), {QStringLiteral("Полноэкранный рендер"), QStringLiteral("全画面表示")}},
        {QStringLiteral("Viewport render size"), {QStringLiteral("Размер рендера вьюпорта"), QStringLiteral("ビューポート描画サイズ")}},
        {QStringLiteral("The viewport follows the window size and aspect."), {QStringLiteral("Вьюпорт следует за размером и пропорциями окна."), QStringLiteral("ビューポートはウィンドウのサイズと比率に追従します。")}},
        {QStringLiteral("Match the window size"), {QStringLiteral("Совпадать с размером окна"), QStringLiteral("ウィンドウサイズに合わせる")}},
        {QStringLiteral("Width"), {QStringLiteral("Ширина"), QStringLiteral("幅")}},
        {QStringLiteral("Height"), {QStringLiteral("Высота"), QStringLiteral("高さ")}},
        {QStringLiteral("Start frame"), {QStringLiteral("Начальный кадр"), QStringLiteral("開始フレーム")}},
        {QStringLiteral("End frame"), {QStringLiteral("Конечный кадр"), QStringLiteral("終了フレーム")}},
        {QStringLiteral("Play / Pause"), {QStringLiteral("Играть / Пауза"), QStringLiteral("再生 / 一時停止")}},
        {QStringLiteral("First frame"), {QStringLiteral("Первый кадр"), QStringLiteral("最初のフレーム")}},
        {QStringLiteral("Previous frame"), {QStringLiteral("Предыдущий кадр"), QStringLiteral("前のフレーム")}},
        {QStringLiteral("Next frame"), {QStringLiteral("Следующий кадр"), QStringLiteral("次のフレーム")}},
        {QStringLiteral("Last frame"), {QStringLiteral("Последний кадр"), QStringLiteral("最後のフレーム")}},
        {QStringLiteral("Current frame"), {QStringLiteral("Текущий кадр"), QStringLiteral("現在のフレーム")}},
        {QStringLiteral("Register keyframe"), {QStringLiteral("Записать ключ"), QStringLiteral("キーを登録")}},
        {QStringLiteral("Delete keyframe"), {QStringLiteral("Удалить ключ"), QStringLiteral("キーを削除")}},
        {QStringLiteral("Zoom"), {QStringLiteral("Масштаб"), QStringLiteral("ズーム")}},
        {QStringLiteral("Select all"), {QStringLiteral("Выбрать все"), QStringLiteral("すべて選択")}},
        {QStringLiteral("Select every bone keyframe at the current frame"), {QStringLiteral("Выбрать все ключи костей на текущем кадре"), QStringLiteral("現在のフレームの全ボーンキーを選択")}},
        {QStringLiteral("Track keys"), {QStringLiteral("Ключи дорожки"), QStringLiteral("トラックのキー")}},
        {QStringLiteral("Select every keyframe on the active track"), {QStringLiteral("Выбрать все ключи активной дорожки"), QStringLiteral("アクティブトラックの全キーを選択")}},
        {QStringLiteral("Delete frame"), {QStringLiteral("Удалить кадр"), QStringLiteral("フレームを削除")}},
        {QStringLiteral("Delete every keyframe at the current frame"), {QStringLiteral("Удалить все ключи на текущем кадре"), QStringLiteral("現在のフレームの全キーを削除")}},
        {QStringLiteral("Copy"), {QStringLiteral("Копировать"), QStringLiteral("コピー")}},
        {QStringLiteral("Paste"), {QStringLiteral("Вставить"), QStringLiteral("貼り付け")}},
        {QStringLiteral("Delete keys"), {QStringLiteral("Удалить ключи"), QStringLiteral("キーを削除")}},
        {QStringLiteral("Delete the selected keyframes"), {QStringLiteral("Удалить выбранные ключи"), QStringLiteral("選択したキーを削除")}},
        {QStringLiteral("Bake VMD"), {QStringLiteral("Запечь VMD"), QStringLiteral("VMDをベイク")}},
        {QStringLiteral("Bake the selected VMD keys into authored tracks at their current frames"), {QStringLiteral("Запечь выбранные ключи VMD в авторские дорожки на их кадрах"), QStringLiteral("選択したVMDキーを現在のフレームで作者トラックにベイク")}},
        {QStringLiteral("Delete VMD"), {QStringLiteral("Удалить VMD"), QStringLiteral("VMDを削除")}},
        {QStringLiteral("Delete the selected keys from the loaded VMD"), {QStringLiteral("Удалить выбранные ключи из загруженного VMD"), QStringLiteral("読み込んだVMDから選択キーを削除")}},
        {QStringLiteral("Audio"), {QStringLiteral("Аудио"), QStringLiteral("オーディオ")}},
        {QStringLiteral("Load audio"), {QStringLiteral("Загрузить аудио"), QStringLiteral("オーディオを読み込み")}},
        {QStringLiteral("Audio offset in timeline frames. Positive values delay the sound."), {QStringLiteral("Смещение аудио в кадрах таймлайна. Положительные значения задерживают звук."), QStringLiteral("タイムラインのフレーム単位のオーディオオフセット。正の値で音が遅れます。")}},
        {QStringLiteral("Volume"), {QStringLiteral("Громкость"), QStringLiteral("音量")}},
        {QStringLiteral("Preview audio volume"), {QStringLiteral("Громкость предпросмотра"), QStringLiteral("プレビュー音量")}},
        {QStringLiteral("Loop"), {QStringLiteral("Повтор"), QStringLiteral("ループ")}},
        {QStringLiteral("Jiggle dynamics"), {QStringLiteral("Динамика покачивания"), QStringLiteral("揺れ物理")}},
        {QStringLiteral("VSync"), {QStringLiteral("Вертикальная синхронизация"), QStringLiteral("垂直同期")}},
        {QStringLiteral("Speed"), {QStringLiteral("Скорость"), QStringLiteral("速度")}},
        {QStringLiteral("Only while playing"), {QStringLiteral("Только при воспроизведении"), QStringLiteral("再生中のみ")}},
        {QStringLiteral("30 FPS"), {QStringLiteral("30 FPS"), QStringLiteral("30 FPS")}},
        {QStringLiteral("60 FPS"), {QStringLiteral("60 FPS"), QStringLiteral("60 FPS")}},
        {QStringLiteral("Unlimited"), {QStringLiteral("Без ограничения"), QStringLiteral("無制限")}},
        {QStringLiteral("Simulation rate: faster response without raising gravity"), {QStringLiteral("Скорость симуляции: быстрее отклик без усиления гравитации"), QStringLiteral("シミュレーション速度: 重力を上げずに応答を速くします")}},
        {QStringLiteral("Automatic curve"), {QStringLiteral("Автоматическая кривая"), QStringLiteral("自動カーブ")}},
        {QStringLiteral("Linear"), {QStringLiteral("Линейная"), QStringLiteral("リニア")}},
        {QStringLiteral("S-Curve"), {QStringLiteral("S-кривая"), QStringLiteral("Sカーブ")}},
        {QStringLiteral("Custom curve..."), {QStringLiteral("Своя кривая..."), QStringLiteral("カスタムカーブ...")}},
        {QStringLiteral("Applies to selected keyframes, or sets the default for new keys when nothing is selected."), {QStringLiteral("Применяется к выбранным ключам или задаёт значение по умолчанию, если ничего не выбрано."), QStringLiteral("選択キーに適用するか、未選択時は新しいキーの既定値になります。")}},
        {QStringLiteral("Interpolation Curve"), {QStringLiteral("Кривая интерполяции"), QStringLiteral("補間カーブ")}},
        {QStringLiteral("The curve is applied to the selected keyframes."), {QStringLiteral("Кривая применяется к выбранным ключам."), QStringLiteral("カーブは選択したキーに適用されます。")}},
        {QStringLiteral("Selected keys use different curves. Applying replaces all of them with the edited curve."), {QStringLiteral("Выбранные ключи используют разные кривые. Применение заменит их все редактируемой кривой."), QStringLiteral("選択キーは異なるカーブを使用しています。適用するとすべて編集したカーブに置き換わります。")}},
        {QStringLiteral("No keyframes are selected. The curve becomes the default for new keyframes."), {QStringLiteral("Ключи не выбраны. Кривая станет значением по умолчанию для новых ключей."), QStringLiteral("キーが選択されていません。カーブは新しいキーの既定値になります。")}},
        {QStringLiteral("mixed"), {QStringLiteral("разные"), QStringLiteral("混在")}},
        {QStringLiteral("Apply"), {QStringLiteral("Применить"), QStringLiteral("適用")}},
        {QStringLiteral("Cancel"), {QStringLiteral("Отмена"), QStringLiteral("キャンセル")}},
        {QStringLiteral("OK"), {QStringLiteral("ОК"), QStringLiteral("OK")}},
        {QStringLiteral("Close"), {QStringLiteral("Закрыть"), QStringLiteral("閉じる")}},
        {QStringLiteral("Global"), {QStringLiteral("Глобально"), QStringLiteral("グローバル")}},
        {QStringLiteral("Local"), {QStringLiteral("Локально"), QStringLiteral("ローカル")}},
        {QStringLiteral("Dist"), {QStringLiteral("Дист."), QStringLiteral("距離")}},
        {QStringLiteral("Position %1"), {QStringLiteral("Позиция %1"), QStringLiteral("位置 %1")}},
        {QStringLiteral("Rotation %1"), {QStringLiteral("Поворот %1"), QStringLiteral("回転 %1")}},
        {QStringLiteral("To Model"), {QStringLiteral("Модель"), QStringLiteral("モデル")}},
        {QStringLiteral("To Camera"), {QStringLiteral("Камера"), QStringLiteral("カメラ")}},
        {QStringLiteral("To model mode: load a model first"), {QStringLiteral("Режим модели: сначала загрузите модель"), QStringLiteral("モデルモード: 先にモデルを読み込んでください")}},        {QStringLiteral("Register the current target at this frame"), {QStringLiteral("Записать текущую цель на этом кадре"), QStringLiteral("現在のターゲットをこのフレームに登録")}},
        {QStringLiteral("Toggle the ground shadow"), {QStringLiteral("Переключить тень на полу"), QStringLiteral("地面影を切り替え")}},
        {QStringLiteral("Show frame statistics in the status bar"), {QStringLiteral("Показывать статистику кадра в строке состояния"), QStringLiteral("ステータスバーにフレーム統計を表示")}},
        {QStringLiteral("Reduce CPU work while editing"), {QStringLiteral("Снизить нагрузку CPU при редактировании"), QStringLiteral("編集中のCPU負荷を下げる")}},
        {QStringLiteral("Coordinate grid"), {QStringLiteral("Координатная сетка"), QStringLiteral("座標グリッド")}},
        {QStringLiteral("Frame the model with the default camera"), {QStringLiteral("Кадрировать модель камерой по умолчанию"), QStringLiteral("既定のカメラでモデルを収める")}},
        {QStringLiteral("Camera  ·  Light  ·  Accessory"), {QStringLiteral("Камера  ·  Свет  ·  Аксессуар"), QStringLiteral("カメラ  ·  ライト  ·  アクセサリ")}},
        {QStringLiteral("Info"), {QStringLiteral("Инфо"), QStringLiteral("情報")}},
        {QStringLiteral("culled"), {QStringLiteral("отсечено"), QStringLiteral("カリング")}},
        {QStringLiteral("Camera distance"), {QStringLiteral("Дистанция камеры"), QStringLiteral("カメラ距離")}},
        {QStringLiteral("Close %1"), {QStringLiteral("Закрыть %1"), QStringLiteral("%1を閉じる")}},
        {QStringLiteral("Show skeleton bones for every visible model, not only the active one"), {QStringLiteral("Показывать кости всех видимых моделей, а не только активной"), QStringLiteral("アクティブモデルだけでなく、表示中の全モデルのボーンを表示")}},
        {QStringLiteral("Place %1"), {QStringLiteral("Позиция %1"), QStringLiteral("配置 %1")}},
        {QStringLiteral("Rot %1"), {QStringLiteral("Поворот %1"), QStringLiteral("回転 %1")}},
        {QStringLiteral("Render target %1 x %2  (%3:%4)"), {QStringLiteral("Цель рендера %1 x %2  (%3:%4)"), QStringLiteral("描画ターゲット %1 x %2  (%3:%4)")}},
        {QStringLiteral("P1 (%1, %2)   P2 (%3, %4)"), {QStringLiteral("P1 (%1, %2)   P2 (%3, %4)"), QStringLiteral("P1 (%1, %2)   P2 (%3, %4)")}},
        {QStringLiteral("FPS"), {QStringLiteral("FPS"), QStringLiteral("FPS")}},
        {QStringLiteral("enabled"), {QStringLiteral("включено"), QStringLiteral("有効")}},
        {QStringLiteral("disabled"), {QStringLiteral("отключено"), QStringLiteral("無効")}},
        {QStringLiteral("authored"), {QStringLiteral("авторский"), QStringLiteral("作者")}},
        {QStringLiteral("Low power"), {QStringLiteral("Экономия"), QStringLiteral("省電力")}},
        {QStringLiteral("Grid"), {QStringLiteral("Сетка"), QStringLiteral("グリッド")}},
        {QStringLiteral("Ground shadow"), {QStringLiteral("Тень на полу"), QStringLiteral("地面影")}},
        {QStringLiteral("Load PMX Model"), {QStringLiteral("Загрузить модель PMX"), QStringLiteral("PMXモデルを読み込み")}},
        {QStringLiteral("Load VMD Motion"), {QStringLiteral("Загрузить движение VMD"), QStringLiteral("VMDモーションを読み込み")}},
        {QStringLiteral("Save VMD Motion"), {QStringLiteral("Сохранить движение VMD"), QStringLiteral("VMDモーションを保存")}},
        {QStringLiteral("Load VPD Pose"), {QStringLiteral("Загрузить позу VPD"), QStringLiteral("VPDポーズを読み込み")}},
        {QStringLiteral("Save VPD Pose"), {QStringLiteral("Сохранить позу VPD"), QStringLiteral("VPDポーズを保存")}},
        {QStringLiteral("Load Hikari motion"), {QStringLiteral("Загрузить движение Hikari"), QStringLiteral("Hikariモーションを読み込み")}},
        {QStringLiteral("Save Hikari motion"), {QStringLiteral("Сохранить движение Hikari"), QStringLiteral("Hikariモーションを保存")}},
        {QStringLiteral("Open Hikari project"), {QStringLiteral("Открыть проект Hikari"), QStringLiteral("Hikariプロジェクトを開く")}},
        {QStringLiteral("Save Hikari project"), {QStringLiteral("Сохранить проект Hikari"), QStringLiteral("Hikariプロジェクトを保存")}},
        {QStringLiteral("Accessory"), {QStringLiteral("Аксессуар"), QStringLiteral("アクセサリ")}},
        {QStringLiteral("Polygon Model eXtended (*.pmx);;All files (*.*)"), {QStringLiteral("Модель PMX (*.pmx);;Все файлы (*.*)"), QStringLiteral("PMXモデル (*.pmx);;すべてのファイル (*.*)")}},
        {QStringLiteral("Vocaloid Motion Data (*.vmd);;All files (*.*)"), {QStringLiteral("Движение VMD (*.vmd);;Все файлы (*.*)"), QStringLiteral("VMDモーション (*.vmd);;すべてのファイル (*.*)")}},
        {QStringLiteral("Vocaloid Motion Data (*.vmd)"), {QStringLiteral("Движение VMD (*.vmd)"), QStringLiteral("VMDモーション (*.vmd)")}},
        {QStringLiteral("Vocaloid Pose Data (*.vpd);;All files (*.*)"), {QStringLiteral("Поза VPD (*.vpd);;Все файлы (*.*)"), QStringLiteral("VPDポーズ (*.vpd);;すべてのファイル (*.*)")}},
        {QStringLiteral("Vocaloid Pose Data (*.vpd)"), {QStringLiteral("Поза VPD (*.vpd)"), QStringLiteral("VPDポーズ (*.vpd)")}},
        {QStringLiteral("Bitmap Image (*.bmp)"), {QStringLiteral("Изображение BMP (*.bmp)"), QStringLiteral("BMP画像 (*.bmp)")}},
        {QStringLiteral("Audio files (*.wav *.mp3 *.flac *.ogg *.m4a *.aac *.opus *.wma *.aiff *.aif *.wv *.ape);;All files (*)"), {QStringLiteral("Аудиофайлы (*.wav *.mp3 *.flac *.ogg *.m4a *.aac *.opus *.wma *.aiff *.aif *.wv *.ape);;Все файлы (*)"), QStringLiteral("オーディオファイル (*.wav *.mp3 *.flac *.ogg *.m4a *.aac *.opus *.wma *.aiff *.aif *.wv *.ape);;すべてのファイル (*)")}},
        {QStringLiteral("Hikari Project (*.hkp)"), {QStringLiteral("Проект Hikari (*.hkp)"), QStringLiteral("Hikariプロジェクト (*.hkp)")}},
        {QStringLiteral("Hikari Motion (*.hkm)"), {QStringLiteral("Движение Hikari (*.hkm)"), QStringLiteral("Hikariモーション (*.hkm)")}},
        {QStringLiteral("DirectX Model (*.x)"), {QStringLiteral("Модель DirectX (*.x)"), QStringLiteral("DirectXモデル (*.x)")}},
        {QStringLiteral("Hikari Studio"), {QStringLiteral("Hikari Studio"), QStringLiteral("Hikari Studio")}},
        {QStringLiteral("Preparing the workspace"), {QStringLiteral("Подготовка рабочей области"), QStringLiteral("ワークスペースを準備中")}},
        {QStringLiteral("Loading the startup scene"), {QStringLiteral("Загрузка стартовой сцены"), QStringLiteral("起動シーンを読み込み中")}},
        {QStringLiteral("STUDIO"), {QStringLiteral("STUDIO"), QStringLiteral("STUDIO")}},
        {QStringLiteral("HIKARI"), {QStringLiteral("HIKARI"), QStringLiteral("HIKARI")}},
        {QStringLiteral("Scene cleared"), {QStringLiteral("Сцена очищена"), QStringLiteral("シーンをクリアしました")}},
        {QStringLiteral("Created new project"), {QStringLiteral("Создан новый проект"), QStringLiteral("新しいプロジェクトを作成しました")}},
        {QStringLiteral("Working directory"), {QStringLiteral("Рабочий каталог"), QStringLiteral("作業ディレクトリ")}},
        {QStringLiteral("Default directory set: %1"), {QStringLiteral("Рабочий каталог: %1"), QStringLiteral("作業ディレクトリを設定: %1")}},
        {QStringLiteral("Render directory"), {QStringLiteral("Каталог рендера"), QStringLiteral("出力ディレクトリ")}},
        {QStringLiteral("Render Animation Frames"), {QStringLiteral("Рендер анимации"), QStringLiteral("アニメーションフレーム出力")}},
        {QStringLiteral("Export Video..."), {QStringLiteral("Экспорт видео..."), QStringLiteral("動画を書き出し...")}},
        {QStringLiteral("Export Video"), {QStringLiteral("Экспорт видео"), QStringLiteral("動画を書き出し")}},
        {QStringLiteral("Browse..."), {QStringLiteral("Обзор..."), QStringLiteral("参照...")}},
        {QStringLiteral("Format"), {QStringLiteral("Формат"), QStringLiteral("形式")}},
        {QStringLiteral("Bitrate"), {QStringLiteral("Битрейт"), QStringLiteral("ビットレート")}},
        {QStringLiteral("Include audio"), {QStringLiteral("Включить аудио"), QStringLiteral("音声を含める")}},
        {QStringLiteral("Exporting video..."), {QStringLiteral("Экспорт видео..."), QStringLiteral("動画を書き出し中...")}},
        {QStringLiteral("Choose an output file."), {QStringLiteral("Укажите выходной файл."), QStringLiteral("出力ファイルを指定してください。")}},
        {QStringLiteral("The end frame must not precede the start frame."), {QStringLiteral("Конечный кадр не может быть раньше начального."), QStringLiteral("終了フレームは開始フレームより前にできません。")}},
        {QStringLiteral("The selected encoder is not available on this system."), {QStringLiteral("Выбранный энкодер недоступен в этой системе."), QStringLiteral("選択したエンコーダーはこのシステムで利用できません。")}},
        {QStringLiteral("Video files (*.mp4 *.mov *.mkv *.ts *.avi)"), {QStringLiteral("Видеофайлы (*.mp4 *.mov *.mkv *.ts *.avi)"), QStringLiteral("動画ファイル (*.mp4 *.mov *.mkv *.ts *.avi)")}},
        {QStringLiteral("Frames are rendered at the viewport render size: %1 x %2."), {QStringLiteral("Кадры рендерятся в размере вьюпорта: %1 x %2."), QStringLiteral("フレームはビューポート描画サイズで出力されます: %1 x %2。")}},
        {QStringLiteral("Render Image"), {QStringLiteral("Изображение"), QStringLiteral("画像出力")}},
        {QStringLiteral("Screen Size - Viewport Render Target"), {QStringLiteral("Размер экрана - цель рендера вьюпорта"), QStringLiteral("画面サイズ - ビューポート描画ターゲット")}},
        {QStringLiteral("Viewport render size: %1 x %2"), {QStringLiteral("Размер рендера вьюпорта: %1 x %2"), QStringLiteral("ビューポート描画サイズ: %1 x %2")}},
        {QStringLiteral("Gravity & Dynamics Configuration"), {QStringLiteral("Настройка гравитации и динамики"), QStringLiteral("重力とダイナミクス設定")}},
        {QStringLiteral("Gravity %1"), {QStringLiteral("Гравитация %1"), QStringLiteral("重力 %1")}},
        {QStringLiteral("Noise"), {QStringLiteral("Шум"), QStringLiteral("ノイズ")}},
        {QStringLiteral("Joint springs preserve PMX stiffness; bodies use Jolt 6DOF limits, CCD and chain projection."), {QStringLiteral("Пружины шарниров сохраняют жёсткость PMX; тела используют ограничения Jolt 6DOF, CCD и проекцию цепей."), QStringLiteral("ジョイントスプリングはPMXの剛性を保持し、剛体はJoltの6DOF制限、CCD、チェーン投影を使用します。")}},
        {QStringLiteral("Earth (-9.8 Y)"), {QStringLiteral("Земля (-9.8 Y)"), QStringLiteral("地球 (-9.8 Y)")}},
        {QStringLiteral("Zero-G"), {QStringLiteral("Невесомость"), QStringLiteral("無重力")}},
        {QStringLiteral("Outside Parent (OP) Register"), {QStringLiteral("Регистрация внешнего родителя (OP)"), QStringLiteral("外部親 (OP) 登録")}},
        {QStringLiteral("Outside parent setting"), {QStringLiteral("Настройка внешнего родителя"), QStringLiteral("外部親設定")}},
        {QStringLiteral("Subject bone"), {QStringLiteral("Кость-субъект"), QStringLiteral("対象ボーン")}},
        {QStringLiteral("Target model"), {QStringLiteral("Целевая модель"), QStringLiteral("ターゲットモデル")}},
        {QStringLiteral("Target bone"), {QStringLiteral("Целевая кость"), QStringLiteral("ターゲットボーン")}},
        {QStringLiteral("Subject bone relative place (auto calculated)"), {QStringLiteral("Относительное положение кости-субъекта (авто)"), QStringLiteral("対象ボーンの相対位置 (自動計算)")}},
        {QStringLiteral("Frame register"), {QStringLiteral("Записать в кадр"), QStringLiteral("フレーム登録")}},
        {QStringLiteral("OP register"), {QStringLiteral("Регистрация OP"), QStringLiteral("OP登録")}},
        {QStringLiteral("Load a PMX model first."), {QStringLiteral("Сначала загрузите модель PMX."), QStringLiteral("先にPMXモデルを読み込んでください。")}},
        {QStringLiteral("About Physics"), {QStringLiteral("О физике"), QStringLiteral("物理について")}},
        {QStringLiteral("Keyboard & Mouse Shortcuts"), {QStringLiteral("Клавиатура и мышь"), QStringLiteral("キーボードとマウス")}},
        {QStringLiteral("Right mouse drag - orbit camera\nMiddle mouse drag - pan camera\nWheel - zoom\nLeft click - select bone\nCorner gizmo - row 1 rotates, row 2 moves\nCorner gizmo title - local / global axes\n\nSpace - play / pause\nV - VSync\nP - physics simulation\nJ - jiggle dynamics\nR - reset physics\n\nUndo restores scene values, model transforms and keyframes. The playhead is not restored."), {QStringLiteral("Правая кнопка мыши - вращение камеры\nСредняя кнопка - панорама\nКолесо - масштаб\nЛевый клик - выбор кости\nУгловой гизмо - верхний ряд вращает, нижний перемещает\nЗаголовок гизмо - локальные / глобальные оси\n\nSpace - играть / пауза\nV - вертикальная синхронизация\nP - физика\nJ - динамика покачивания\nR - сброс физики\n\nОтмена восстанавливает значения сцены, трансформы моделей и ключи. Плейхед не восстанавливается."), QStringLiteral("右ドラッグ - カメラ回転\n中ドラッグ - カメラパン\nホイール - ズーム\n左クリック - ボーン選択\nコーナーギズモ - 上段は回転、下段は移動\nギズモ見出し - ローカル / グローバル軸\n\nSpace - 再生 / 一時停止\nV - 垂直同期\nP - 物理シミュレーション\nJ - 揺れ物理\nR - 物理リセット\n\n元に戻すはシーン値、モデルトランスフォーム、キーを復元します。プレイヘッドは復元されません。")}},
        {QStringLiteral("Jolt Physics 5.x\n\n• 60 Hz substepping with distance constraint chains\n• 6DOF spring joints matching PMX rest frames\n• Rest-pose overlap protection and kinematic bone driving\n• SIMD rigid bodies: spheres, boxes and capsules\n• Multithreaded solver and CCD on kinematic limbs"), {QStringLiteral("Jolt Physics 5.x\n\n• Подшаги 60 Гц с цепочками ограничений расстояния\n• 6DOF пружинные шарниры по rest-кадрам PMX\n• Защита от пересечений в rest-позе и кинематическое управление костями\n• SIMD-тела: сферы, боксы и капсулы\n• Многопоточный решатель и CCD на кинематических конечностях"), QStringLiteral("Jolt Physics 5.x\n\n• 距離制約チェーンによる60Hzサブステップ\n• PMXレストフレームに合わせた6DOFスプリングジョイント\n• レストポーズの重なり防止とキネマティックボーン駆動\n• SIMD剛体: 球、ボックス、カプセル\n• マルチスレッドソルバーとキネマティック部位のCCD")}},
        {QStringLiteral("HIKARI STUDIO\n\nDirect3D 11 MMD/MME runtime and animation studio.\nC++20, Qt 6, Jolt Physics, vcpkg and ccache.\n\nShader Model 5.0, shader reflection, MRT, compute shaders and a 64 KB MME constant buffer."), {QStringLiteral("HIKARI STUDIO\n\nСреда выполнения MMD/MME на Direct3D 11 и студия анимации.\nC++20, Qt 6, Jolt Physics, vcpkg и ccache.\n\nShader Model 5.0, рефлексия шейдеров, MRT, compute-шейдеры и константный буфер MME 64 КБ."), QStringLiteral("HIKARI STUDIO\n\nDirect3D 11 MMD/MMEランタイムとアニメーションスタジオ。\nC++20、Qt 6、Jolt Physics、vcpkg、ccache。\n\nShader Model 5.0、シェーダーリフレクション、MRT、コンピュートシェーダー、64KB MME定数バッファ。")}},
        {QStringLiteral("Sets the offscreen resolution and aspect ratio used to render the 3D viewport. The application window is not resized; the image is letterboxed to fit."), {QStringLiteral("Задаёт разрешение и пропорции внеэкранного буфера для рендера 3D-вьюпорта. Окно приложения не меняется; изображение вписывается с полями."), QStringLiteral("3Dビューポートのオフスクリーン解像度と縦横比を設定します。アプリケーションウィンドウは変更されず、画像はレターボックスで収まります。")}},
        {QStringLiteral("Undo restores scene values, model transforms and keyframes. The playhead is not restored."), {QStringLiteral("Отмена восстанавливает значения сцены, трансформы моделей и ключи. Плейхед не восстанавливается."), QStringLiteral("元に戻すはシーン値、モデルトランスフォーム、キーを復元します。プレイヘッドは復元されません。")}},
        {QStringLiteral("Hikari Studio ready"), {QStringLiteral("Hikari Studio готов"), QStringLiteral("Hikari Studio 準備完了")}},
        {QStringLiteral("vertices"), {QStringLiteral("вершин"), QStringLiteral("頂点")}},
        {QStringLiteral("bones"), {QStringLiteral("костей"), QStringLiteral("ボーン")}},
        {QStringLiteral("Failed to open model: %1"), {QStringLiteral("Не удалось открыть модель: %1"), QStringLiteral("モデルを開けません: %1")}},
        {QStringLiteral("Failed to load model: %1"), {QStringLiteral("Не удалось загрузить модель: %1"), QStringLiteral("モデルを読み込めません: %1")}},
        {QStringLiteral("Loaded model: %1"), {QStringLiteral("Модель загружена: %1"), QStringLiteral("モデルを読み込みました: %1")}},
        {QStringLiteral("Failed to open motion: %1"), {QStringLiteral("Не удалось открыть движение: %1"), QStringLiteral("モーションを開けません: %1")}},
        {QStringLiteral("Failed to parse motion: %1"), {QStringLiteral("Не удалось разобрать движение: %1"), QStringLiteral("モーションを解析できません: %1")}},
        {QStringLiteral("Loaded motion: %1"), {QStringLiteral("Движение загружено: %1"), QStringLiteral("モーションを読み込みました: %1")}},
        {QStringLiteral("Loaded camera motion: %1"), {QStringLiteral("Движение камеры загружено: %1"), QStringLiteral("カメラモーションを読み込みました: %1")}},
        {QStringLiteral("Loaded model motion: %1"), {QStringLiteral("Движение модели загружено: %1"), QStringLiteral("モデルモーションを読み込みました: %1")}},
        {QStringLiteral("Motion has no keyframes: %1"), {QStringLiteral("В движении нет ключевых кадров: %1"), QStringLiteral("モーションにキーフレームがありません: %1")}},
        {QStringLiteral("Loaded VPD: %1"), {QStringLiteral("Поза VPD загружена: %1"), QStringLiteral("VPDを読み込みました: %1")}},
        {QStringLiteral("Saved VPD: %1"), {QStringLiteral("Поза VPD сохранена: %1"), QStringLiteral("VPDを保存しました: %1")}},
        {QStringLiteral("Failed to read VPD: %1"), {QStringLiteral("Не удалось прочитать VPD: %1"), QStringLiteral("VPDを読み込めません: %1")}},
        {QStringLiteral("Failed to parse VPD: %1"), {QStringLiteral("Не удалось разобрать VPD: %1"), QStringLiteral("VPDを解析できません: %1")}},
        {QStringLiteral("Failed to write VPD: %1"), {QStringLiteral("Не удалось записать VPD: %1"), QStringLiteral("VPDを書き込めません: %1")}},
        {QStringLiteral("Loaded audio: %1"), {QStringLiteral("Аудио загружено: %1"), QStringLiteral("オーディオを読み込みました: %1")}},
        {QStringLiteral("Failed to load audio: %1"), {QStringLiteral("Не удалось загрузить аудио: %1"), QStringLiteral("オーディオを読み込めません: %1")}},
        {QStringLiteral("Failed to start audio playback: %1"), {QStringLiteral("Не удалось запустить воспроизведение: %1"), QStringLiteral("オーディオ再生を開始できません: %1")}},
        {QStringLiteral("Saved motion: %1 (%2 keyframes)"), {QStringLiteral("Движение сохранено: %1 (%2 ключей)"), QStringLiteral("モーションを保存しました: %1 (%2キー)")}},
        {QStringLiteral("Failed to write VMD: %1"), {QStringLiteral("Не удалось записать VMD: %1"), QStringLiteral("VMDを書き込めません: %1")}},
        {QStringLiteral("Saved Hikari motion: %1"), {QStringLiteral("Движение Hikari сохранено: %1"), QStringLiteral("Hikariモーションを保存しました: %1")}},
        {QStringLiteral("Loaded Hikari motion: %1"), {QStringLiteral("Движение Hikari загружено: %1"), QStringLiteral("Hikariモーションを読み込みました: %1")}},
        {QStringLiteral("Failed to save Hikari motion: %1"), {QStringLiteral("Не удалось сохранить движение Hikari: %1"), QStringLiteral("Hikariモーションを保存できません: %1")}},
        {QStringLiteral("Failed to read Hikari motion: %1"), {QStringLiteral("Не удалось прочитать движение Hikari: %1"), QStringLiteral("Hikariモーションを読み込めません: %1")}},
        {QStringLiteral("Failed to parse Hikari motion: %1"), {QStringLiteral("Не удалось разобрать движение Hikari: %1"), QStringLiteral("Hikariモーションを解析できません: %1")}},
        {QStringLiteral("Saved project: %1"), {QStringLiteral("Проект сохранён: %1"), QStringLiteral("プロジェクトを保存しました: %1")}},
        {QStringLiteral("Loaded project: %1"), {QStringLiteral("Проект загружен: %1"), QStringLiteral("プロジェクトを読み込みました: %1")}},
        {QStringLiteral("Failed to save project: %1"), {QStringLiteral("Не удалось сохранить проект: %1"), QStringLiteral("プロジェクトを保存できません: %1")}},
        {QStringLiteral("Failed to read project: %1"), {QStringLiteral("Не удалось прочитать проект: %1"), QStringLiteral("プロジェクトを読み込めません: %1")}},
        {QStringLiteral("Failed to parse project: %1"), {QStringLiteral("Не удалось разобрать проект: %1"), QStringLiteral("プロジェクトを解析できません: %1")}},
        {QStringLiteral("Project contains no loadable models: %1"), {QStringLiteral("В проекте нет загружаемых моделей: %1"), QStringLiteral("読み込めるモデルがありません: %1")}},
        {QStringLiteral("Project model could not be loaded: %1"), {QStringLiteral("Модель проекта не загружена: %1"), QStringLiteral("プロジェクトのモデルを読み込めません: %1")}},
        {QStringLiteral("Screenshot saved to: %1"), {QStringLiteral("Скриншот сохранён: %1"), QStringLiteral("スクリーンショットを保存しました: %1")}},
        {QStringLiteral("Capture failed: %1"), {QStringLiteral("Ошибка захвата: %1"), QStringLiteral("キャプチャに失敗: %1")}},
        {QStringLiteral("Rendered frames %1..%2 to: %3"), {QStringLiteral("Кадры %1..%2 отрендерены в: %3"), QStringLiteral("フレーム %1..%2 を出力: %3")}},
        {QStringLiteral("Invalid frame range: %1..%2"), {QStringLiteral("Неверный диапазон кадров: %1..%2"), QStringLiteral("無効なフレーム範囲: %1..%2")}},
        {QStringLiteral("Invalid video export range or path"), {QStringLiteral("Неверный диапазон или путь экспорта видео"), QStringLiteral("動画書き出しの範囲またはパスが無効です")}},
        {QStringLiteral("Video export failed: %1"), {QStringLiteral("Ошибка экспорта видео: %1"), QStringLiteral("動画の書き出しに失敗: %1")}},
        {QStringLiteral("Video export cancelled"), {QStringLiteral("Экспорт видео отменён"), QStringLiteral("動画の書き出しをキャンセルしました")}},
        {QStringLiteral("Exported video: %1"), {QStringLiteral("Видео экспортировано: %1"), QStringLiteral("動画を書き出しました: %1")}},
        {QStringLiteral("Frame failed: %1"), {QStringLiteral("Ошибка кадра: %1"), QStringLiteral("フレーム失敗: %1")}},
        {QStringLiteral("Frame failed with an unknown error"), {QStringLiteral("Кадр завершился неизвестной ошибкой"), QStringLiteral("フレームが不明なエラーで失敗")}},
        {QStringLiteral("Direct3D 11 initialization failed: %1"), {QStringLiteral("Ошибка инициализации Direct3D 11: %1"), QStringLiteral("Direct3D 11の初期化に失敗: %1")}},
        {QStringLiteral("Active model: %1"), {QStringLiteral("Активная модель: %1"), QStringLiteral("アクティブモデル: %1")}},
        {QStringLiteral("Active model removed from scene"), {QStringLiteral("Активная модель удалена из сцены"), QStringLiteral("アクティブモデルをシーンから削除しました")}},
        {QStringLiteral("All models removed from scene"), {QStringLiteral("Все модели удалены из сцены"), QStringLiteral("すべてのモデルをシーンから削除しました")}},
        {QStringLiteral("Model position: %1, %2, %3"), {QStringLiteral("Позиция модели: %1, %2, %3"), QStringLiteral("モデル位置: %1, %2, %3")}},
        {QStringLiteral("Model rotation updated"), {QStringLiteral("Поворот модели обновлён"), QStringLiteral("モデルの回転を更新しました")}},
        {QStringLiteral("Model scale: %1x"), {QStringLiteral("Масштаб модели: %1x"), QStringLiteral("モデルスケール: %1x")}},
        {QStringLiteral("Undo"), {QStringLiteral("Отмена"), QStringLiteral("元に戻す")}},
        {QStringLiteral("Redo"), {QStringLiteral("Повтор"), QStringLiteral("やり直し")}},
        {QStringLiteral("Registered keyframe for '%1' at frame %2"), {QStringLiteral("Ключ для «%1» записан на кадре %2"), QStringLiteral("「%1」のキーをフレーム%2に登録しました")}},
        {QStringLiteral("Deleted keyframe for '%1' at frame %2"), {QStringLiteral("Ключ для «%1» удалён на кадре %2"), QStringLiteral("「%1」のキーをフレーム%2から削除しました")}},
        {QStringLiteral("Deleted %1 keyframe(s)"), {QStringLiteral("Удалено ключей: %1"), QStringLiteral("キーを%1個削除しました")}},
        {QStringLiteral("Deleted keyframes at frame %1"), {QStringLiteral("Ключи на кадре %1 удалены"), QStringLiteral("フレーム%1のキーを削除しました")}},
        {QStringLiteral("Pasted %1 keyframe(s)"), {QStringLiteral("Вставлено ключей: %1"), QStringLiteral("キーを%1個貼り付けました")}},
        {QStringLiteral("Copied %1 keyframe(s)"), {QStringLiteral("Скопировано ключей: %1"), QStringLiteral("キーを%1個コピーしました")}},
        {QStringLiteral("Applied interpolation curve to %1 keyframe(s)"), {QStringLiteral("Кривая интерполяции применена к ключам: %1"), QStringLiteral("補間カーブを%1個のキーに適用しました")}},
        {QStringLiteral("Interpolation curve set as the default for new keyframes"), {QStringLiteral("Кривая задана по умолчанию для новых ключей"), QStringLiteral("新しいキーの既定カーブを設定しました")}},
        {QStringLiteral("Registered IK state for %1 bone(s) at frame %2"), {QStringLiteral("Состояние IK записано для костей: %1 на кадре %2"), QStringLiteral("IK状態を%1個のボーンにフレーム%2で登録しました")}},
        {QStringLiteral("Registered display/IK/OP state for '%1' (%2 OP link(s)) at frame %3"), {QStringLiteral("Состояние Disp/IK/OP для «%1» (%2 связей OP) записано на кадре %3"), QStringLiteral("「%1」の表示/IK/OP状態 (%2個のOPリンク) をフレーム%3に登録しました")}},
        {QStringLiteral("Deleted %1 VMD keyframe(s)"), {QStringLiteral("Удалено ключей VMD: %1"), QStringLiteral("VMDキーを%1個削除しました")}},
        {QStringLiteral("Baked %1 VMD keyframe(s) into authored tracks"), {QStringLiteral("Запечено ключей VMD в авторские дорожки: %1"), QStringLiteral("VMDキーを%1個作者トラックにベイクしました")}},
        {QStringLiteral("No VMD keys could be baked"), {QStringLiteral("Не удалось запечь ни одного ключа VMD"), QStringLiteral("ベイクできるVMDキーがありません")}},
        {QStringLiteral("Moved %1 VMD keyframe(s) by %2 frame(s)"), {QStringLiteral("Перемещено ключей VMD: %1 на %2 кадр(ов)"), QStringLiteral("VMDキーを%1個、%2フレーム移動しました")}},
        {QStringLiteral("Moving %1 VMD keyframe(s) by %2 frame(s)"), {QStringLiteral("Перемещение ключей VMD: %1 на %2 кадр(ов)"), QStringLiteral("VMDキーを%1個、%2フレーム移動中")}},
        {QStringLiteral("Moved %1 keyframe(s)"), {QStringLiteral("Перемещено ключей: %1"), QStringLiteral("キーを%1個移動しました")}},
        {QStringLiteral("Selected %1 keyframe(s)"), {QStringLiteral("Выбрано ключей: %1"), QStringLiteral("キーを%1個選択しました")}},
        {QStringLiteral("Selected %1 keyframe %2 at frame %3"), {QStringLiteral("Выбран %1 ключ %2 на кадре %3"), QStringLiteral("%1キー %2 をフレーム%3で選択しました")}},
        {QStringLiteral("Selected bone"), {QStringLiteral("Кость выбрана"), QStringLiteral("ボーンを選択")}},
        {QStringLiteral("Selected bone: %1"), {QStringLiteral("Выбрана кость: %1"), QStringLiteral("ボーンを選択: %1")}},
        {QStringLiteral("Selected bone [%1]: '%2'"), {QStringLiteral("Выбрана кость [%1]: «%2»"), QStringLiteral("ボーンを選択 [%1]: 「%2」")}},
        {QStringLiteral("Selected morph track: %1"), {QStringLiteral("Выбрана дорожка морфа: %1"), QStringLiteral("モーフトラックを選択: %1")}},
        {QStringLiteral("Selected all bone keyframes at the current frame"), {QStringLiteral("Выбраны все ключи костей на текущем кадре"), QStringLiteral("現在のフレームの全ボーンキーを選択しました")}},
        {QStringLiteral("Editing the camera"), {QStringLiteral("Редактирование камеры"), QStringLiteral("カメラを編集")}},
        {QStringLiteral("Editing the model"), {QStringLiteral("Редактирование модели"), QStringLiteral("モデルを編集")}},
        {QStringLiteral("Physics enabled"), {QStringLiteral("Физика включена"), QStringLiteral("物理を有効にしました")}},
        {QStringLiteral("Physics disabled"), {QStringLiteral("Физика отключена"), QStringLiteral("物理を無効にしました")}},
        {QStringLiteral("Physics reset requested"), {QStringLiteral("Запрошен сброс физики"), QStringLiteral("物理リセットを要求しました")}},
        {QStringLiteral("Bone angles and translations initialized"), {QStringLiteral("Углы и смещения кости сброшены"), QStringLiteral("ボーンの角度と移動を初期化しました")}},
        {QStringLiteral("Reset all facial morphs"), {QStringLiteral("Все морфы лица сброшены"), QStringLiteral("表情モーフをすべてリセットしました")}},
        {QStringLiteral("Loaded accessory: %1"), {QStringLiteral("Аксессуар загружен: %1"), QStringLiteral("アクセサリを読み込みました: %1")}},
        {QStringLiteral("Registered relative pose for '%1'"), {QStringLiteral("Относительная поза записана для «%1»"), QStringLiteral("「%1」の相対ポーズを登録しました")}},
        {QStringLiteral("OP registration applied to '%1'"), {QStringLiteral("Регистрация OP применена к «%1»"), QStringLiteral("「%1」にOP登録を適用しました")}},
        {QStringLiteral("IK %1 for bone '%2'"), {QStringLiteral("IK %1 для кости «%2»"), QStringLiteral("ボーン「%2」のIKを%1")}},
        {QStringLiteral("Select VMD keys to bake"), {QStringLiteral("Выберите ключи VMD для запекания"), QStringLiteral("ベイクするVMDキーを選択してください")}},
    };
    return entries;
}

QString language_value(Language language) {
    switch (language) {
    case Language::Russian:
        return QStringLiteral("ru");
    case Language::Japanese:
        return QStringLiteral("ja");
    default:
        return QStringLiteral("en");
    }
}

void set_property_text(QObject* object, const char* /*property*/, const char* source_property,
                       const std::function<void(const QString&)>& setter,
                       const QString& current) {
    if (object->property(kSkip).toBool()) {
        return;
    }
    QString source;
    if (object->property(source_property).isValid()) {
        source = object->property(source_property).toString();
    } else {
        source = current;
        object->setProperty(source_property, source);
    }
    setter(message(source));
}

QString translated(const Entry& entry) {
    return g_language == Language::Russian ? entry.ru : entry.ja;
}

QString pattern_to_regex(const QString& pattern) {
    QString result = QStringLiteral("^");
    QString literal;
    const auto flush = [&result, &literal] {
        if (!literal.isEmpty()) {
            result += QRegularExpression::escape(literal);
            literal.clear();
        }
    };
    for (int i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == QLatin1Char('%') && i + 1 < pattern.size() &&
            pattern[i + 1].isDigit()) {
            flush();
            result += QStringLiteral("(.*?)");
            ++i;
        } else {
            literal += pattern[i];
        }
    }
    flush();
    result += QStringLiteral("$");
    return result;
}

QString substitute(const QString& pattern, const QRegularExpressionMatch& match) {
    QString result;
    for (int i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == QLatin1Char('%') && i + 1 < pattern.size() &&
            pattern[i + 1].isDigit()) {
            const int group = pattern[i + 1].digitValue();
            result += match.captured(group);
            ++i;
        } else {
            result += pattern[i];
        }
    }
    return result;
}

} // namespace

Language current_language() {
    return g_language;
}

QString language_code(Language language) {
    return language_value(language);
}

Language language_from_code(const QString& code) {
    if (code == QStringLiteral("ru")) {
        return Language::Russian;
    }
    if (code == QStringLiteral("ja")) {
        return Language::Japanese;
    }
    return Language::English;
}

QString language_name(Language language) {
    switch (language) {
    case Language::Russian:
        return QStringLiteral("Русский");
    case Language::Japanese:
        return QStringLiteral("日本語");
    default:
        return QStringLiteral("English");
    }
}

void load_language() {
    QSettings settings;
    const QString code = settings.value(QString::fromLatin1(kSetting), QStringLiteral("en"))
                             .toString();
    g_language = language_from_code(code);
}

void set_language(Language language) {
    g_language = language;
    QSettings settings;
    settings.setValue(QString::fromLatin1(kSetting), language_value(language));
}

QString text(const QString& english) {
    if (g_language == Language::English) {
        return english;
    }
    const auto it = dictionary().find(english);
    if (it == dictionary().end()) {
        return english;
    }
    return translated(it.value());
}

QString message(const QString& formatted) {
    if (g_language == Language::English) {
        return formatted;
    }
    if (const auto it = dictionary().find(formatted); it != dictionary().end()) {
        return translated(it.value());
    }
    for (auto it = dictionary().begin(); it != dictionary().end(); ++it) {
        if (!it.key().contains(QLatin1Char('%'))) {
            continue;
        }
        const QRegularExpression expression(pattern_to_regex(it.key()));
        const QRegularExpressionMatch match = expression.match(formatted);
        if (match.hasMatch()) {
            return substitute(translated(it.value()), match);
        }
    }
    return formatted;
}

void skip(QWidget* widget) {
    if (widget != nullptr) {
        widget->setProperty(kSkip, true);
    }
}

void apply(QWidget* root) {
    if (root == nullptr) {
        return;
    }
    const QList<QWidget*> found = root->findChildren<QWidget*>();
    QList<QWidget*> widgets = found;
    widgets.prepend(root);
    for (QWidget* widget : widgets) {
        if (auto* label = qobject_cast<QLabel*>(widget)) {
            set_property_text(widget, "text", kText,
                              [label](const QString& value) { label->setText(value); },
                              label->text());
        } else if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
            set_property_text(widget, "text", kText,
                              [button](const QString& value) { button->setText(value); },
                              button->text());
        } else if (auto* group = qobject_cast<QGroupBox*>(widget)) {
            set_property_text(widget, "title", kText,
                              [group](const QString& value) { group->setTitle(value); },
                              group->title());
        } else if (auto* menu = qobject_cast<QMenu*>(widget)) {
            set_property_text(widget, "title", kText,
                              [menu](const QString& value) { menu->setTitle(value); },
                              menu->title());
        } else if (auto* dock = qobject_cast<QDockWidget*>(widget)) {
            set_property_text(widget, "windowTitle", kText,
                              [dock](const QString& value) { dock->setWindowTitle(value); },
                              dock->windowTitle());
        } else if (auto* tab = qobject_cast<QTabWidget*>(widget)) {
            if (!widget->property(kSkip).toBool()) {
                QStringList sources;
                if (widget->property(kTabs).isValid()) {
                    sources = widget->property(kTabs).toStringList();
                } else {
                    for (int i = 0; i < tab->count(); ++i) {
                        sources.append(tab->tabText(i));
                    }
                    widget->setProperty(kTabs, sources);
                }
                for (int i = 0; i < tab->count() && i < sources.size(); ++i) {
                    tab->setTabText(i, hikari::studio::localization::text(sources[i]));
                }
            }
        } else if (auto* combo = qobject_cast<QComboBox*>(widget)) {
            if (widget->property(kSkip).toBool()) {
                continue;
            }
            QStringList sources;
            if (widget->property(kItems).isValid()) {
                sources = widget->property(kItems).toStringList();
            } else {
                for (int i = 0; i < combo->count(); ++i) {
                    sources.append(combo->itemText(i));
                }
                widget->setProperty(kItems, sources);
            }
            combo->blockSignals(true);
            for (int i = 0; i < combo->count() && i < sources.size(); ++i) {
                combo->setItemText(i, hikari::studio::localization::text(sources[i]));
            }
            combo->blockSignals(false);
        } else if (auto* edit = qobject_cast<QLineEdit*>(widget)) {
            set_property_text(widget, "placeholderText", kText,
                              [edit](const QString& value) { edit->setPlaceholderText(value); },
                              edit->placeholderText());
        }
        if (auto* window = qobject_cast<QMainWindow*>(widget); window != nullptr) {
            set_property_text(widget, "windowTitle", kText,
                              [window](const QString& value) { window->setWindowTitle(value); },
                              window->windowTitle());
        } else if (auto* dialog = qobject_cast<QDialog*>(widget); dialog != nullptr) {
            set_property_text(widget, "windowTitle", kText,
                              [dialog](const QString& value) { dialog->setWindowTitle(value); },
                              dialog->windowTitle());
        }
    }

    const QList<QAction*> actions = root->findChildren<QAction*>();
    if (auto* window = qobject_cast<QMainWindow*>(root)) {
        if (window->menuBar() != nullptr) {
            for (QAction* action : window->menuBar()->actions()) {
                set_property_text(action, "text", kText,
                                  [action](const QString& value) { action->setText(value); },
                                  action->text());
                if (action->menu() != nullptr) {
                    apply(action->menu());
                }
            }
        }
    }
    for (QAction* action : actions) {
        set_property_text(action, "text", kText,
                          [action](const QString& value) { action->setText(value); },
                          action->text());
        set_property_text(action, "toolTip", kToolTip,
                          [action](const QString& value) { action->setToolTip(value); },
                          action->toolTip());
        set_property_text(action, "statusTip", kStatusTip,
                          [action](const QString& value) { action->setStatusTip(value); },
                          action->statusTip());
    }
}

} // namespace hikari::studio::localization
