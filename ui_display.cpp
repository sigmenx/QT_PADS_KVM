#include "ui_display.h"
#include "ElaTheme.h"

#include <QDebug>
#include <QDateTime>
#include <QFileDialog>
#include <QFrame>
#include <QFont>
#include <QSerialPortInfo>

ui_display::ui_display(QString camdevPath, QWidget *parent)
    : QWidget(parent), // 修改为继承 QWidget
      m_camdevPath(camdevPath)
{
    setAttribute(Qt::WA_DeleteOnClose); // 关闭即销毁对象
    // 1. 初始化对象
    m_VideoManager = new VideoController(this);  //视频处理线程管理器
    m_HidManager = new HidController(this); //鼠标键盘事件管理器

    m_topbarvisiable=false;
    // 2. 纯 UI 设置
    setWindowTitle("KVMDISPLAY");
    resize(1024, 768); // 默认大小
    initUI();
    // 3.初始化摄像头 ===
    startCameraLogic();
    // 4. 初始化HID设备参数 ===
    startSerialLogic();
}

ui_display::~ui_display()
{
    // quitThread包含wait函数
    if (m_VideoManager) {
        m_VideoManager->quitThread(); // 这会阻塞直到 run() 结束
    }
    if (m_HidManager) {
        delete m_HidManager;
        m_HidManager = nullptr;
    }
}

void ui_display::closeEvent(QCloseEvent *event) {
    emit windowClosed(); // 当窗口关闭时发送信号
    event->accept();
}

// ==========================================
// HID设备通信逻辑
// ==========================================
//初始化逻辑
void ui_display::startSerialLogic()
{
    // 1. 填充串口列表 (UartSelect)
    cmb_hid_UartSelect->clear();
    // 获取所有可用串口
    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : infos) {
        // 显示文本：COM3 (USB-SERIAL CH340)
        QString label = info.portName();
        if(!info.description().isEmpty()) {
            label += " (" + info.description() + ")";
        }
        // 数据：COM3
        cmb_hid_UartSelect->addItem(label, info.portName());
    }
    // 默认选中第一个
    if (cmb_hid_UartSelect->count() > 0) {
        cmb_hid_UartSelect->setCurrentIndex(0);
    }

    // 2. 填充波特率 (BtrSelect)
    // CH9329 默认通常是 9600 或 115200，这里列出常用值
    QList<int> baudRates = {9600, 19200, 38400, 57600, 115200};

    // 复用你的通用模板函数
    updateComboBox<int>(cmb_hid_BtrSelect, baudRates, [](const int& rate){
        return QString::number(rate);
    });
    // 尝试默认选中 9600
    cmb_hid_BtrSelect->setCurrentIndex(0);

    // 3. 填充设备 (DevSelect) - 固定 CH9329
    cmb_hid_DevSelect->clear();
    cmb_hid_DevSelect->addItem("CH9329");
    cmb_hid_DevSelect->setCurrentIndex(0);
}

//参数更改逻辑
void ui_display::on_btn_hid_SetApply_clicked()
{
    // 0. 安全校验
    if (!m_HidManager || !cmb_hid_UartSelect || !cmb_hid_BtrSelect) return;
    if (cmb_hid_UartSelect->count() == 0) return;

    // 1. 获取串口参数
    // 注意：保留你原程序的逻辑，手动添加 "/dev/" 前缀
    QString portName = cmb_hid_UartSelect->currentData().toString();
    //QString portName = "/dev/" + cmb_hid_UartSelect->currentData().toString();
    int baudRate = cmb_hid_BtrSelect->currentData().toInt();

    // 2. 通过处理类尝试连接
    // openSerial 内部封装了 closeDevice, init 和 checkConnection
    //bool isConnected = m_HidManager->openSerial(portName, baudRate);
    bool isConnected = m_HidManager->initDriver(portName, baudRate);

    // 3. 更新单选框使能状态
    rbt_hid_AbsMode->setEnabled(isConnected);
    rbt_hid_RefMode->setEnabled(isConnected);

    // 4. 根据结果更新 UI 和 逻辑状态
    if (isConnected) {
        // === 成功逻辑 ===
        lbl_vid_HIDStatus->setText("通信成功");
        lbl_vid_HIDStatus->setStyleSheet("color: #00CC00; border: none; padding: 0px;"); // 绿色高亮

        // 连接成功后，默认进入绝对坐标模式
        rbt_hid_AbsMode->setChecked(true);

        // 【关键】同步告诉处理器当前是绝对模式
        m_HidManager->setControlMode(MODE_ABSOLUTE);

        //更改HID设备的初始图像分辨率/显示大小
        m_HidManager->setSourceResolution(cmb_vid_ResSelect->currentData().toSize(),lbl_ui_VideoShow->size());

    } else {
        // === 失败逻辑 ===
        lbl_vid_HIDStatus->setText("通信失败");
        lbl_vid_HIDStatus->setStyleSheet("color: #FF0000; border: none; padding: 0px;"); // 红色高亮

        // 选中 "HID关" (失能控制)
        if (rbt_hid_EnCtrl) rbt_hid_EnCtrl->setChecked(true);

        // 刷新串口列表 (保留你原程序的逻辑，可能是为了让用户重选)
        startSerialLogic();
    }
}

// ==========================================
// 视频处理线程相关
// ==========================================
// 初始化逻辑
void ui_display::startCameraLogic()
{
    if (!m_VideoManager) return;

    // 在主线程打开设备（仅打开 fd，不启动流）
    if (m_VideoManager->m_camera->openDevice(m_camdevPath)) {

        // --- 填充 UI 逻辑 ---
        cmb_vid_FmtSelect->blockSignals(true);
        cmb_vid_FmtSelect->clear();
        auto formats = m_VideoManager->m_camera->getSupportedFormats(); // 此时 fd 已开，可以获取格式
        for(const auto &fmt : formats) {
            cmb_vid_FmtSelect->addItem(fmt.first, QVariant(fmt.second));
        }
        cmb_vid_FmtSelect->blockSignals(false);

        // 连接信号并启动线程
        connect(m_VideoManager, &VideoController::frameReady, this, &ui_display::handleFrame);
        m_VideoManager->start(); // 启动循环，但此时 m_pause 为 true，线程会 wait

        // 触发首次配置
        if(cmb_vid_FmtSelect->count() > 0) {
            on_cmb_vid_FmtSelect_currentIndexChanged(0);
            on_btn_vid_SetApply_clicked(); // 这里会调用 updateSettings 并唤醒线程
        }
    }
}

// === 帧处理槽函数 ===
//void ui_display::handleFrame(QImage image)
//{
//    // 此时 image 已经是从子线程传来的完整数据
//    //lbl_ui_VideoShow->setPixmap(QPixmap::fromImage(image));
//    if (!image.isNull()) {
//        QSize labelSize = lbl_ui_VideoShow->size();
//        QPixmap scaledPix = QPixmap::fromImage(image).scaled(labelSize, Qt::KeepAspectRatio, Qt::FastTransformation);
//        lbl_ui_VideoShow->setPixmap(scaledPix);
//    }
//}
void ui_display::handleFrame(QImage image)
{
    if (image.isNull() || !lbl_ui_VideoShow) return;

    // 1. 获取 Label 尺寸
    QSize labelSize = lbl_ui_VideoShow->size();

    // 2. 先在 CPU 中缩放 QImage (效率更高)
    // 使用 Qt::FastTransformation 保证预览流畅度，若需画质可改用 Qt::SmoothTransformation
    QImage scaledImg = image.scaled(labelSize, Qt::KeepAspectRatio, Qt::FastTransformation);

    // 3. 仅转换缩放后的图像到 Pixmap
    lbl_ui_VideoShow->setPixmap(QPixmap::fromImage(scaledImg));
}

//应用视频修改
void ui_display::on_btn_vid_SetApply_clicked()
{
    unsigned int fmt = cmb_vid_FmtSelect->currentData().toUInt();
    QSize sz = cmb_vid_ResSelect->currentData().toSize();
    int fps = cmb_vid_FpsSelect->currentData().toInt();

    // 直接调用 updateSettings，线程内部会自动暂停、重配、重启
    m_VideoManager->updateSettings(sz.width(), sz.height(), fmt, fps);

    // 更新 UI 状态
    btn_vid_StrOn->setAwesome(ElaIconType::Pause);
}

// 暂停/继续
void ui_display::on_btn_vid_StrOn_clicked()
{
    if (btn_vid_StrOn->getAwesome() == ElaIconType::Play) {
        // 继续采集
        m_VideoManager->startCapturing();
        btn_vid_StrOn->setAwesome(ElaIconType::Pause);
    } else {
        // 暂停采集
        m_VideoManager->stopCapturing();
        btn_vid_StrOn->setAwesome(ElaIconType::Play);
    }
}

// ==========================================
// 视频业务逻辑槽函数
// ==========================================

// 1. 格式改变 -> 刷新分辨率列表
void ui_display::on_cmb_vid_FmtSelect_currentIndexChanged(int index)
{
    Q_UNUSED(index);
    if (!cmb_vid_FmtSelect || !cmb_vid_ResSelect || cmb_vid_FmtSelect->currentIndex() < 0) return;
    // 获取当前格式 ID
    unsigned int fmt = cmb_vid_FmtSelect->currentData().toUInt();
    // 获取后端数据
    QList<QSize> sizes = m_VideoManager->m_camera->getResolutions(fmt);
    // === 修改后：一行调用通用函数 ===
    updateComboBox<QSize>(cmb_vid_ResSelect, sizes, [](const QSize& s){
        return QString("%1x%2").arg(s.width()).arg(s.height());
    });
    // 手动触发下一级联动（刷新帧率）
    if (cmb_vid_ResSelect->count() > 0) {
        on_cmb_vid_ResSelect_currentIndexChanged(0);
    }
}

// 2. 分辨率改变 -> 刷新帧率列表
void ui_display::on_cmb_vid_ResSelect_currentIndexChanged(int index)
{
    Q_UNUSED(index);
    if (!cmb_vid_ResSelect || !cmb_vid_FpsSelect || cmb_vid_ResSelect->currentIndex() < 0) return;
    unsigned int fmt = cmb_vid_FmtSelect->currentData().toUInt();
    QSize sz = cmb_vid_ResSelect->currentData().toSize(); // 确保头文件引用了 QSize
    // 获取后端数据
    QList<int> fpsList = m_VideoManager->m_camera->getFramerates(fmt, sz.width(), sz.height());
    // === 修改后：一行调用通用函数 ===
    updateComboBox<int>(cmb_vid_FpsSelect, fpsList, [](const int& fps){
        return QString("%1 FPS").arg(fps);
    });
}

//截图
void ui_display::on_btn_vid_PicCap_clicked()
{
    // 获取当前显示的图像
    const QPixmap *pix = lbl_ui_VideoShow->pixmap();
    if (pix && !pix->isNull()) {
        QString fileName = QFileDialog::getSaveFileName(this, "保存截图", "", "Images (*.png *.jpg)");
        if (!fileName.isEmpty()) {
            pix->save(fileName);
            qDebug() << "截图已保存:" << fileName;
        }
    }
}

// ==========================================
// 界面交互槽函数
// ==========================================

void ui_display::on_btn_ui_HideSide_clicked()
{
    if (m_sideBarWidget->isVisible()) {
        m_sideBarWidget->hide();
        btn_ui_HideSide->setAwesome(ElaIconType::AngleLeft); // 箭头向左
    } else {
        m_sideBarWidget->show();
        btn_ui_HideSide->setAwesome(ElaIconType::AngleRight); // 箭头向右
    }

    if (!m_topbarvisiable){
        if(m_sideBarWidget->isVisible()){
            m_topWidget->show();
        }else{
            m_topWidget->hide();
        }
    }
}

void ui_display::on_btn_vid_FullScr_clicked()
{
    if (this->isFullScreen()) {
        this->showNormal();
        btn_vid_FullScr->setAwesome(ElaIconType::Expand);
    } else {
        this->showFullScreen();
        btn_vid_FullScr->setAwesome(ElaIconType::Compress);
    }
}

void ui_display::on_btn_ui_HideTop_clicked()
{
    m_topbarvisiable = !m_topbarvisiable;
    // 定义常量样式
    static const QString STYLE_ACTIVE =
        "ElaIconButton { background-color: #0078D4; border-radius: 4px; border: 1px solid #005A9E; }"
        "ElaIconButton:hover { background-color: #005A9E; }";
    static const QString STYLE_NORMAL =
        "ElaIconButton { background-color: #FFFFFF; border-radius: 4px; border: 0px solid #C0C0C0; }"
        "ElaIconButton:hover { background-color: #E6E6E6; border: 1px solid #A0A0A0; }";

    if (m_topbarvisiable) {
        btn_ui_HideTop->setStyleSheet(STYLE_ACTIVE);
        btn_ui_HideTop->setLightIconColor(Qt::white);
    } else {
        btn_ui_HideTop->setStyleSheet(STYLE_NORMAL);
        btn_ui_HideTop->setLightIconColor(Qt::black);
    }
}

// ==========================================
// 界面初始化部分
// ==========================================
void ui_display::initUI()
{
    // 主布局：垂直
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    // 这里设置为0是可以的，因为操作系统会提供外部边框
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 1. 顶部
    initTopBar();
    mainLayout->addWidget(m_topWidget);

    // 2. 中间 (包含视频和侧边栏)
    QWidget *centerContainer = new QWidget(this);
    QHBoxLayout *centerLayout = new QHBoxLayout(centerContainer);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);

    // 2.1 视频部分（视频lbl+隐藏按钮）
    initCenter(centerContainer);
    // 2.2 侧边部分
    initSideBar();

    centerLayout->addWidget(m_videoContainer, 1); // 权重1，拉伸
    centerLayout->addWidget(m_sideBarWidget, 0);  // 权重0，固定

    // 添加到主窗口布局
    mainLayout->addWidget(centerContainer);
}

void ui_display::initTopBar()
{
    m_topWidget = new QWidget(this);
    m_topWidget->setFixedHeight(60);

    // ===== 1. 定义统一的样式常量（仅保留topWidget背景，Label改为透明）=====
    //const QString TOP_BG_COLOR = "#f2f2f2";
    m_topWidget->setStyleSheet(QString("background-color: #f2f2f2; border-bottom: 1px solid #dcdcdc;"));

    QHBoxLayout *topLayout = new QHBoxLayout(m_topWidget);
    topLayout->setContentsMargins(10, 5, 10, 5);
    topLayout->setSpacing(5);

    // === 1. 样式准备 ===
    QFont labelFont;
    labelFont.setPointSize(11);
    // 定义通用 Label 样式
    QString labelStyle = "QLabel { background-color: transparent; color: #333333; border: none; padding: 0px; }";

    // === 辅助 Lambda 1：按钮通用设置  ===
    QString iconBtnStyle = "ElaIconButton { background-color: #FFFFFF; border: 0px solid #C0C0C0; border-radius: 4px; }"
                             "ElaIconButton:hover { background-color: #E6E6E6; border: 1px solid #A0A0A0; }";
    auto setupIconButton = [&](ElaIconButton* btn) {
        btn->setFixedSize(34, 34);
        btn->setStyleSheet(iconBtnStyle);
        btn->setAttribute(Qt::WA_StyledBackground, true);
    };
    // === 辅助 Lambda2：添加带有标题的控制组 ===
    // 自动添加：分隔线 -> 标题 Label -> 控件列表
    auto addControlGroup = [&](QString title, const std::vector<QWidget*>& widgets) {
        // 1. 加竖线
        QFrame *line = new QFrame(this);
        line->setFrameShape(QFrame::VLine);
        line->setStyleSheet("color: #A0A0A0;");
        line->setFixedHeight(24);
        topLayout->addSpacing(5);
        topLayout->addWidget(line);
        topLayout->addSpacing(5);

        // 2. 加标题
        QLabel *lbl = new QLabel(title, this);
        lbl->setStyleSheet(labelStyle);
        lbl->setFont(labelFont); // 假设 labelFont 在外部定义了
        topLayout->addWidget(lbl);
        topLayout->addSpacing(5);

        // 3. 加控件
        for(auto w : widgets) {
            topLayout->addWidget(w);
        }
    };

    // === 2. 实例化控件 ===
    // --- 隐藏顶部按钮（注释保留）---
    btn_ui_HideTop = new ElaIconButton(ElaIconType::Desktop, this);
    setupIconButton(btn_ui_HideTop);
    connect(btn_ui_HideTop, &ElaIconButton::clicked, this, &ui_display::on_btn_ui_HideTop_clicked);

    // --- HID 部分控件 ---
    lbl_vid_HIDStatus = new QLabel("等待操作",this);
    lbl_vid_HIDStatus->setFont(labelFont);
    lbl_vid_HIDStatus->setStyleSheet(labelStyle);

    rbt_hid_EnCtrl = new ElaRadioButton("关闭",this);
    rbt_hid_EnCtrl->setFont(labelFont);
    rbt_hid_EnCtrl->setChecked(true);

    rbt_hid_AbsMode = new ElaRadioButton("普通",this);
    rbt_hid_AbsMode->setFont(labelFont);
    rbt_hid_AbsMode->setEnabled(false);

    rbt_hid_RefMode = new ElaRadioButton("触控",this);
    rbt_hid_RefMode->setFont(labelFont);
    rbt_hid_RefMode->setEnabled(false);

    //////////////////////////////////////////////////////////////////////////////////
    // [禁用模式]
    connect(rbt_hid_EnCtrl, &ElaRadioButton::toggled, this, [=](bool checked){
        if (checked) {
            m_HidManager->setControlMode(MODE_NONE);
            // 可以在这里恢复鼠标样式为普通箭头
            if(lbl_ui_VideoShow) lbl_ui_VideoShow->setCursor(Qt::ArrowCursor);
            qDebug() << "HID Mode: None";
        }
    });
    // [绝对模式] (点击操作)
    connect(rbt_hid_AbsMode, &ElaRadioButton::toggled, this, [=](bool checked){
        if (checked) {
            m_HidManager->setControlMode(MODE_ABSOLUTE);
            // 绝对模式下，通常使用十字光标方便定位
            if(lbl_ui_VideoShow) lbl_ui_VideoShow->setCursor(Qt::CrossCursor);
            qDebug() << "HID Mode: Absolute";
        }
    });
    // [相对模式] (触控操作)
    connect(rbt_hid_RefMode, &ElaRadioButton::toggled, this, [=](bool checked){
        if (checked) {
            m_HidManager->setControlMode(MODE_RELATIVE);
            // 相对模式下，设置光标为手掌
            if(lbl_ui_VideoShow) lbl_ui_VideoShow->setCursor(Qt::OpenHandCursor);
            qDebug() << "HID Mode: Relative";
        }
    });
    //////////////////////////////////////////////////////////////////////////////////

    cmb_hid_MutKeySel = new ElaComboBox(this);
    cmb_hid_MedKeySel = new ElaComboBox(this);

    btn_hid_KeySend = new ElaPushButton("发送", this);
    btn_hid_KeySend->setFixedWidth(60);

    // --- 视频控制图标 ---
    btn_vid_FullScr = new ElaIconButton(ElaIconType::Expand, this);
    setupIconButton(btn_vid_FullScr);
    connect(btn_vid_FullScr, &ElaIconButton::clicked, this, &ui_display::on_btn_vid_FullScr_clicked);

    btn_vid_StrOn = new ElaIconButton(ElaIconType::Pause, this);
    setupIconButton(btn_vid_StrOn);
    connect(btn_vid_StrOn, &ElaIconButton::clicked, this, &ui_display::on_btn_vid_StrOn_clicked);

    btn_vid_PicCap = new ElaIconButton(ElaIconType::Camera, this);
    setupIconButton(btn_vid_PicCap);
    connect(btn_vid_PicCap, &ElaIconButton::clicked, this, &ui_display::on_btn_vid_PicCap_clicked);

    // --- 音频图标 ---
    btn_aud_OnOff = new ElaIconButton(ElaIconType::VolumeHigh, this);
    setupIconButton(btn_aud_OnOff);

    // === 3. 布局添加 ===
    topLayout->addWidget(btn_ui_HideTop);
    topLayout->addSpacing(5);
    topLayout->addWidget(lbl_vid_HIDStatus);
    // 第一组：HID基础
    addControlGroup("键鼠", {rbt_hid_EnCtrl, rbt_hid_AbsMode, rbt_hid_RefMode});
    // 第二组：HID快捷键 (使用 Lambda)
    addControlGroup("快捷键", {cmb_hid_MutKeySel, cmb_hid_MedKeySel, btn_hid_KeySend});
    // 第三组：视频控制 (使用 Lambda)
    addControlGroup("视频控制", {btn_vid_FullScr, btn_vid_StrOn, btn_vid_PicCap});
    // 第四组：音频控制 (使用 Lambda)
    addControlGroup("音频控制", {btn_aud_OnOff});

    topLayout->addStretch();
}

void ui_display::initCenter(QWidget * centerContainer)
{
    // === 左侧视频容器 ===
    m_videoContainer = new QWidget(centerContainer);
    m_videoContainer->setStyleSheet("background-color: black;"); // 黑底

    // 1. 视频 Label (底层背景)
    lbl_ui_VideoShow = new QLabel("Loading Signal...", m_videoContainer);
    lbl_ui_VideoShow->setAlignment(Qt::AlignCenter);
    lbl_ui_VideoShow->setStyleSheet("color: white; font-size: 20px;");
    // 忽略尺寸策略，允许缩放
    lbl_ui_VideoShow->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    lbl_ui_VideoShow->setScaledContents(false);

    //键盘鼠标HIDKVM设备设置类
    // === 将 VideoLabel 的事件交给 HidProcess 处理 ===
    lbl_ui_VideoShow->setMouseTracking(false); // 相对模式不需要时刻追踪，只在按下时追踪，具体看逻辑需要
    lbl_ui_VideoShow->installEventFilter(m_HidManager); // 拦截鼠标
    // === 将 整个窗口 的键盘事件也交给 HidProcess 处理 ===
    this->setFocusPolicy(Qt::StrongFocus); // 确保能接收键盘
    this->installEventFilter(m_HidManager); // 拦截键盘

    // 2. 悬浮按钮 (顶层控件)
    // 注意：不再需要手动 move，也不需要后续计算坐标
    btn_ui_HideSide = new ElaIconButton(ElaIconType::AngleRight, m_videoContainer);
    btn_ui_HideSide->setFixedSize(40, 40);
    btn_ui_HideSide->setBorderRadius(20);
    btn_ui_HideSide->setLightHoverColor(QColor(255, 255, 255, 200));
    btn_ui_HideSide->setLightIconColor(Qt::red);
    connect(btn_ui_HideSide, &ElaIconButton::clicked, this, &ui_display::on_btn_ui_HideSide_clicked);

    // === 核心修改：使用 Grid 布局实现自动堆叠与定位 ===
    QGridLayout *vidLayout = new QGridLayout(m_videoContainer);
    vidLayout->setContentsMargins(0, 0, 0, 0);

    // 第一步：把视频 Label 铺满 (0,0)
    vidLayout->addWidget(lbl_ui_VideoShow, 0, 0);

    // 第二步：把按钮也加到 (0,0)，但指定 "右对齐 + 垂直居中"
    // 布局管理器会自动把它叠在上面，并随着窗口大小变化自动调整位置
    vidLayout->addWidget(btn_ui_HideSide, 0, 0, Qt::AlignRight | Qt::AlignVCenter);

    // 第三步：确保按钮在最上层 (虽然通常后添加的在上面，但这行更保险)
    btn_ui_HideSide->raise();
}
void ui_display::initSideBar()
{
    m_sideBarWidget = new QWidget(this);
    m_sideBarWidget->setFixedWidth(200);
    m_sideBarWidget->setStyleSheet("background-color: #fafafa; border-left: 1px solid #d0d0d0;");

    QVBoxLayout *sideLayout = new QVBoxLayout(m_sideBarWidget);
    sideLayout->setContentsMargins(15, 15, 15, 15);
    sideLayout->setSpacing(15);

    // === 定义统一的 Label 样式 (透明背景) ===
    QString transparentStyle = "QLabel { background-color: transparent; color: #333333; border: none; padding: 0px; }";
    QFont labelFont;
    labelFont.setPointSize(11);
    // === 核心优化：定义辅助 Lambda 函数 ===
    // 作用：自动创建Label，应用样式，并将 Label 和 对应的控件(Widget) 一起加入布局
    auto addSettingItem = [&](QBoxLayout* layout, QString text, QWidget* widget) {
        QLabel* lbl = new QLabel(text, m_sideBarWidget);
        lbl->setStyleSheet(transparentStyle);
        lbl->setFont(labelFont);
        layout->addWidget(lbl);    // 加 Label
        layout->addWidget(widget); // 加 控件 (如 ComboBox)
    };

    // --- 2.1 视频设置部分 ---
    QGroupBox *grpVideo = new QGroupBox("视频设置", m_sideBarWidget);
    QVBoxLayout *vBox = new QVBoxLayout(grpVideo);

    // 初始化控件
    cmb_vid_FmtSelect = new ElaComboBox(grpVideo);
    cmb_vid_ResSelect = new ElaComboBox(grpVideo);
    cmb_vid_FpsSelect = new ElaComboBox(grpVideo);
    btn_vid_SetApply = new ElaPushButton("应用视频修改", grpVideo);

    // 连接信号
    connect(cmb_vid_FmtSelect, QOverload<int>::of(&ElaComboBox::currentIndexChanged),
            this, &ui_display::on_cmb_vid_FmtSelect_currentIndexChanged);
    connect(cmb_vid_ResSelect, QOverload<int>::of(&ElaComboBox::currentIndexChanged),
            this, &ui_display::on_cmb_vid_ResSelect_currentIndexChanged);
    connect(btn_vid_SetApply, &ElaPushButton::clicked, this, &ui_display::on_btn_vid_SetApply_clicked);

    // === 修改后：极简的添加逻辑 ===
    addSettingItem(vBox, "格式:", cmb_vid_FmtSelect);
    addSettingItem(vBox, "分辨率:", cmb_vid_ResSelect);
    addSettingItem(vBox, "帧率:", cmb_vid_FpsSelect);
    vBox->addWidget(btn_vid_SetApply); // 按钮单独加

    // --- 2.2 HID设置部分 ---
    QGroupBox *grpHid = new QGroupBox("HID设置", m_sideBarWidget);
    QVBoxLayout *hBox = new QVBoxLayout(grpHid);

    cmb_hid_UartSelect = new ElaComboBox(grpHid);
    cmb_hid_BtrSelect = new ElaComboBox(grpHid);
    cmb_hid_DevSelect = new ElaComboBox(grpHid);
    btn_hid_SetApply = new ElaPushButton("应用HID修改", grpHid);

    // === 修改后：极简的添加逻辑 ===
    addSettingItem(hBox, "串口选择:", cmb_hid_UartSelect);
    addSettingItem(hBox, "波特率:", cmb_hid_BtrSelect);
    addSettingItem(hBox, "设备选择:", cmb_hid_DevSelect);
    hBox->addWidget(btn_hid_SetApply);
    connect(btn_hid_SetApply, &ElaPushButton::clicked, this, &ui_display::on_btn_hid_SetApply_clicked);

    // 添加 GroupBox 到主侧边栏
    sideLayout->addWidget(grpVideo);
    sideLayout->addWidget(grpHid);
    sideLayout->addStretch();
}
