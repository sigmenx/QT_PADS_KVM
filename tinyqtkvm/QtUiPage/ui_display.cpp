#include "ui_display.h"

#include <QDebug>
#include <QFileDialog>
#include <QSerialPortInfo>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QMessageBox>

// === 通用样式常量定义 ===
namespace Styles {
    const QString LABEL_FONT = "QLabel { background-color: transparent; color: #333333; border: none; padding: 0px; font-size: 11pt; }";
    const QString ICON_BTN = "ElaIconButton { background-color: #FFFFFF; border-radius: 4px; } ElaIconButton:hover { background-color: #E6E6E6; border: 1px solid #A0A0A0; }";
    const QString RADIO_BTN = "QLabel { background-color: transparent; color: #333333; border: none; padding: 0px; font-size: 11pt; }";
    static const QString HIDETOP_BTN_ACTIVE = "ElaIconButton { background-color: #0078D4; border-radius: 4px; border: 1px solid #005A9E; }"
                                              "ElaIconButton:hover { background-color: #005A9E; }";
    static const QString HIDETOP_BTN_NORMAL = "ElaIconButton { background-color: #FFFFFF; border-radius: 4px; border: 0px solid #C0C0C0; }"
                                              "ElaIconButton:hover { background-color: #E6E6E6; border: 1px solid #A0A0A0; }";
}

ui_display::ui_display(QString camdevPath, QWidget *parent)
    : QWidget(parent), // 继承 QWidget
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
    //    初始化网络HID数据接收槽函数
    //connect(m_VideoManager, &VideoController::remoteHidPacketReceived, m_HidManager, &HidController::onNetworkHidReceived);
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
    bool isConnected = m_HidManager->initDriver(portName, baudRate);

    // 3. 更新单选框使能状态
    rbt_hid_AbsMode->setEnabled(isConnected);
    rbt_hid_RefMode->setEnabled(isConnected);

    // 4. 根据结果更新 UI 和 逻辑状态
    if (isConnected) {
        // === 成功逻辑 ===
        lbl_vid_HIDStatus->setText("通信成功");
        lbl_vid_HIDStatus->setStyleSheet("QLabel { background-color: transparent; color: #00CC00; border: none; padding: 0px; font-size: 11pt; }");// 绿色高亮

        // 连接成功后，默认进入绝对坐标模式
        rbt_hid_AbsMode->setChecked(true);

        // 【关键】同步告诉处理器当前是绝对模式
        m_HidManager->setControlMode(MODE_ABSOLUTE);

        //更改HID设备的初始图像分辨率/显示大小
        m_HidManager->setSourceResolution(cmb_vid_ResSelect->currentData().toSize(),lbl_ui_VideoShow->size());

    } else {
        // === 失败逻辑 ===
        lbl_vid_HIDStatus->setText("通信失败");
        lbl_vid_HIDStatus->setStyleSheet("QLabel { background-color: transparent; color: #FF0000; border: none; padding: 0px; font-size: 11pt; }");// 红色高亮

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

    if(btn_web_Start->getIsToggled() && fmt != V4L2_PIX_FMT_YUYV){
        QMessageBox::warning(this, "提示", "请先关闭IP-KVM");
        return;
    }

    // 直接调用 updateSettings，线程内部会自动暂停、重配、重启
    m_VideoManager->updateSettings(sz.width(), sz.height(), fmt, fps);

    // 同步通知 HID 控制器源分辨率已变更
    if (m_HidManager && lbl_ui_VideoShow) {
        // 传入新的源分辨率 (sz) 和当前的控件大小
        m_HidManager->setSourceResolution(sz, lbl_ui_VideoShow->size());
    }

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
// 网络转发业务逻辑槽函数
// ==========================================
void ui_display::on_btn_web_Start_clicked()
{
    bool iswebRunning = btn_web_Start->getIsToggled();  //true为开启
    if (iswebRunning) {
        // === 准备开启 ===
        // 1. 获取当前格式和端口
        unsigned int currentFmt = cmb_vid_FmtSelect->currentData().toUInt();
        bool portOk = false;
        int port = txt_web_Port->text().toInt(&portOk);

        // 2. 校验：必须是 YUYV 格式 且 端口有效
        if (currentFmt == V4L2_PIX_FMT_YUYV && portOk && port > 0 && port <= 65535) {
            // 3. 尝试启动
            if (m_VideoManager->startServer(port)) {
                // 成功：改变按钮状态
                btn_web_Start->setText("停止");
            } else {
                QMessageBox::critical(this, "错误", "启动服务失败，端口可能被占用");
                btn_web_Start->setIsToggled(false);
            }
        } else {
            // 失败：弹窗提示
            QMessageBox::warning(this, "提示", "格式未置YUYV或端口错误");
            btn_web_Start->setIsToggled(false);
        }
    } else {
        // === 准备停止 ===
        m_VideoManager->stopServer();
        // 恢复按钮状态
        btn_web_Start->setText("开启");
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
    if (m_topbarvisiable) {
        btn_ui_HideTop->setStyleSheet(Styles::HIDETOP_BTN_ACTIVE);
        btn_ui_HideTop->setLightIconColor(Qt::white);
    } else {
        btn_ui_HideTop->setStyleSheet(Styles::HIDETOP_BTN_NORMAL);
        btn_ui_HideTop->setLightIconColor(Qt::black);
    }
}

// ==========================================
// 界面初始化部分 辅助函数实现
// ==========================================

// 统一设置容器样式
void ui_display::applyContainerStyle(QWidget* widget, int w, int h, const QString& color) {
    if (h != 0) widget->setFixedHeight(h);
    if (w != 0) widget->setFixedWidth(w);
    widget->setStyleSheet(QString("background-color:%1; border-bottom: 1px solid #dcdcdc;").arg(color));
}

// 快速创建图标按钮工厂函数
ElaIconButton* ui_display::createIconButton(ElaIconType::IconName icon, const char* memberSlot) {
    ElaIconButton* btn = new ElaIconButton(icon, this);
    btn->setFixedSize(34, 34);
    btn->setStyleSheet(Styles::ICON_BTN);
    btn->setAttribute(Qt::WA_StyledBackground, true);
    if (memberSlot) {
        connect(btn, SIGNAL(clicked()), this, memberSlot);
    }
    return btn;
}

// 顶部栏组生成器
void ui_display::addTopControlGroup(QHBoxLayout* layout, const QString &title, const std::initializer_list<QWidget*> &widgets) {
    // 1. 分隔线
    QFrame *line = new QFrame(this);
    line->setFrameShape(QFrame::VLine);
    line->setStyleSheet("color: #A0A0A0;");
    line->setFixedHeight(24);

    layout->addSpacing(5);
    layout->addWidget(line);
    layout->addSpacing(5);

    // 2. 标题
    QLabel *lbl = new QLabel(title, this);
    lbl->setStyleSheet(Styles::LABEL_FONT);
    layout->addWidget(lbl);
    layout->addSpacing(5);

    // 3. 批量添加控件
    for (auto w : widgets) {
        layout->addWidget(w);
    }
}

// 侧边栏行生成器
void ui_display::addSideSettingItem(QLayout* layout, const QString &text, QWidget *widget) {
    QLabel* lbl = new QLabel(text, this);
    lbl->setStyleSheet(Styles::LABEL_FONT);
    layout->addWidget(lbl);
    layout->addWidget(widget);
}

// ==========================================
// 界面初始化实现
// ==========================================

void ui_display::initUI()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    initTopBar();
    mainLayout->addWidget(m_topWidget);

    QWidget *centerContainer = new QWidget(this);
    QHBoxLayout *centerLayout = new QHBoxLayout(centerContainer);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);

    initCenter(centerContainer); // 初始化视频区
    initSideBar();               // 初始化侧边栏

    centerLayout->addWidget(m_videoContainer, 1);
    centerLayout->addWidget(m_sideBarWidget, 0);

    mainLayout->addWidget(centerContainer);
}

void ui_display::initTopBar()
{
    m_topWidget = new QWidget(this);
    applyContainerStyle(m_topWidget, 0, 60, "#f2f2f2");

    QHBoxLayout *topLayout = new QHBoxLayout(m_topWidget);
    topLayout->setContentsMargins(5, 5, 5, 5);
    topLayout->setSpacing(5);

    // 1. 创建左侧隐藏按钮
    btn_ui_HideTop = createIconButton(ElaIconType::Desktop, SLOT(on_btn_ui_HideTop_clicked()));

    // 2. HID 状态标签
    lbl_vid_HIDStatus = new QLabel("等待操作", this);
    lbl_vid_HIDStatus->setStyleSheet(Styles::LABEL_FONT);

    // 3. 创建单选按钮 (利用 Lambda 简化设置)
    auto setupRadio = [&](const QString& text, bool checked = false) {
        ElaRadioButton* rb = new ElaRadioButton(text, this);
        rb->setStyleSheet(Styles::RADIO_BTN);
        rb->setEnabled(checked);
        if(checked) rb->setChecked(true);
        return rb;
    };

    rbt_hid_EnCtrl = setupRadio("关闭", true);
    rbt_hid_AbsMode = setupRadio("普通");
    rbt_hid_RefMode = setupRadio("触控");

    // 连接信号逻辑
    connect(rbt_hid_EnCtrl, &ElaRadioButton::toggled, this, [=](bool c){ if(c) {
            m_HidManager->setControlMode(MODE_NONE); if(lbl_ui_VideoShow) lbl_ui_VideoShow->setCursor(Qt::ArrowCursor); }});

    connect(rbt_hid_AbsMode, &ElaRadioButton::toggled, this, [=](bool c){ if(c) {
            m_HidManager->setControlMode(MODE_ABSOLUTE); if(lbl_ui_VideoShow) lbl_ui_VideoShow->setCursor(Qt::CrossCursor); }});

    connect(rbt_hid_RefMode, &ElaRadioButton::toggled, this, [=](bool c){ if(c) {
            m_HidManager->setControlMode(MODE_RELATIVE); if(lbl_ui_VideoShow) lbl_ui_VideoShow->setCursor(Qt::OpenHandCursor); }});

    // 4. 其他控件
    cmb_hid_MutKeySel = new ElaComboBox(this);
    cmb_hid_MedKeySel = new ElaComboBox(this);
    btn_hid_KeySend = new ElaPushButton("发送", this);
    btn_hid_KeySend->setFixedWidth(60);

    // 5. 视频/音频按钮 (使用工厂函数)
    btn_vid_FullScr = createIconButton(ElaIconType::Expand, SLOT(on_btn_vid_FullScr_clicked()));
    btn_vid_StrOn = createIconButton(ElaIconType::Pause, SLOT(on_btn_vid_StrOn_clicked()));
    btn_vid_PicCap = createIconButton(ElaIconType::Camera, SLOT(on_btn_vid_PicCap_clicked()));
    btn_aud_OnOff = createIconButton(ElaIconType::VolumeHigh, nullptr);

    // === 组装布局 (极简模式) ===
    topLayout->addWidget(btn_ui_HideTop);
    topLayout->addSpacing(5);
    topLayout->addWidget(lbl_vid_HIDStatus);

    addTopControlGroup(topLayout, "键鼠", {rbt_hid_EnCtrl, rbt_hid_AbsMode, rbt_hid_RefMode});
    addTopControlGroup(topLayout, "快捷键", {cmb_hid_MutKeySel, cmb_hid_MedKeySel, btn_hid_KeySend});
    addTopControlGroup(topLayout, "视频控制", {btn_vid_FullScr, btn_vid_StrOn, btn_vid_PicCap});
    addTopControlGroup(topLayout, "音频控制", {btn_aud_OnOff});

    topLayout->addStretch();
}

void ui_display::initCenter(QWidget *centerContainer)
{
    m_videoContainer = new QWidget(centerContainer);
    m_videoContainer->setStyleSheet("background-color: black;");

    // 1. 视频标签
    lbl_ui_VideoShow = new QLabel("Loading Signal...", m_videoContainer);
    lbl_ui_VideoShow->setAlignment(Qt::AlignCenter);
    lbl_ui_VideoShow->setStyleSheet("color: white; font-size: 20px;");
    lbl_ui_VideoShow->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    // 安装事件过滤器
    lbl_ui_VideoShow->setMouseTracking(true);

    lbl_ui_VideoShow->installEventFilter(m_HidManager);
    this->setFocusPolicy(Qt::StrongFocus);
    this->installEventFilter(m_HidManager);

    // 2. 悬浮按钮
    btn_ui_HideSide = new ElaIconButton(ElaIconType::AngleRight, m_videoContainer);
    btn_ui_HideSide->setFixedSize(40, 40);
    btn_ui_HideSide->setBorderRadius(20);
    btn_ui_HideSide->setLightHoverColor(QColor(255, 255, 255, 200));
    btn_ui_HideSide->setLightIconColor(Qt::red);
    connect(btn_ui_HideSide, &ElaIconButton::clicked, this, &ui_display::on_btn_ui_HideSide_clicked);

    // 3. 堆叠布局
    QGridLayout *vidLayout = new QGridLayout(m_videoContainer);
    vidLayout->setContentsMargins(0, 0, 0, 0);
    vidLayout->addWidget(lbl_ui_VideoShow, 0, 0);
    vidLayout->addWidget(btn_ui_HideSide, 0, 0, Qt::AlignRight | Qt::AlignVCenter);

    btn_ui_HideSide->raise();
}

void ui_display::initSideBar()
{
    m_sideBarWidget = new QWidget(this);
    applyContainerStyle(m_sideBarWidget, 170, 0, "#fafafa");

    QVBoxLayout *sideLayout = new QVBoxLayout(m_sideBarWidget);
    sideLayout->setContentsMargins(5, 5, 5, 5);
    sideLayout->setSpacing(0);

    // --- 1. 视频设置 Group ---
    QGroupBox *grpVideo = new QGroupBox("视频设置", m_sideBarWidget);
    QVBoxLayout *vBox = new QVBoxLayout(grpVideo);

    cmb_vid_FmtSelect = new ElaComboBox(grpVideo);
    cmb_vid_ResSelect = new ElaComboBox(grpVideo);
    cmb_vid_FpsSelect = new ElaComboBox(grpVideo);
    btn_vid_SetApply = new ElaPushButton("应用视频修改", grpVideo);

    connect(cmb_vid_FmtSelect, QOverload<int>::of(&ElaComboBox::currentIndexChanged), this, &ui_display::on_cmb_vid_FmtSelect_currentIndexChanged);
    connect(cmb_vid_ResSelect, QOverload<int>::of(&ElaComboBox::currentIndexChanged), this, &ui_display::on_cmb_vid_ResSelect_currentIndexChanged);
    connect(btn_vid_SetApply, &ElaPushButton::clicked, this, &ui_display::on_btn_vid_SetApply_clicked);

    addSideSettingItem(vBox, "格式:", cmb_vid_FmtSelect);
    addSideSettingItem(vBox, "分辨率:", cmb_vid_ResSelect);
    addSideSettingItem(vBox, "帧率:", cmb_vid_FpsSelect);
    vBox->addWidget(btn_vid_SetApply);

    // --- 2. HID 设置 Group ---
    QGroupBox *grpHid = new QGroupBox("HID设置", m_sideBarWidget);
    QVBoxLayout *hBox = new QVBoxLayout(grpHid);

    cmb_hid_UartSelect = new ElaComboBox(grpHid);
    cmb_hid_BtrSelect = new ElaComboBox(grpHid);
    cmb_hid_DevSelect = new ElaComboBox(grpHid);
    btn_hid_SetApply = new ElaPushButton("应用HID修改", grpHid);

    connect(btn_hid_SetApply, &ElaPushButton::clicked, this, &ui_display::on_btn_hid_SetApply_clicked);

    addSideSettingItem(hBox, "串口选择:", cmb_hid_UartSelect);
    addSideSettingItem(hBox, "波特率:", cmb_hid_BtrSelect);
    addSideSettingItem(hBox, "设备选择:", cmb_hid_DevSelect);
    hBox->addWidget(btn_hid_SetApply);

    // --- 3. IP-KVM Group ---
    QGroupBox *grpIpKvm = new QGroupBox("IP-KVM", m_sideBarWidget);
    QVBoxLayout *aBox = new QVBoxLayout(grpIpKvm);

    // IP 显示逻辑
    QString localIp = "127.0.0.1";
    for (const QHostAddress &address : QNetworkInterface::allAddresses()) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && address != QHostAddress::LocalHost) {
             localIp = address.toString();
             if(localIp.startsWith("192.168.")) break;
        }
    }

    // 手动布局 IP 行 (因为它比较特殊，有两个Label)
    QHBoxLayout *layIp = new QHBoxLayout();
    QLabel* lblIpTag = new QLabel("IP:", grpIpKvm); lblIpTag->setStyleSheet(Styles::LABEL_FONT);
    QLabel* lblIpVal = new QLabel(localIp, grpIpKvm); lblIpVal->setStyleSheet(Styles::LABEL_FONT);
    lblIpVal->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layIp->addWidget(lblIpTag);
    layIp->addWidget(lblIpVal);
    layIp->addStretch();

    // 端口行
    QHBoxLayout *layPort = new QHBoxLayout();
    QLabel* lblPort = new QLabel("端口:", grpIpKvm); lblPort->setStyleSheet(Styles::LABEL_FONT);
    txt_web_Port = new QLineEdit("8080", grpIpKvm);
    btn_web_Start = new ElaToggleButton("开启", grpIpKvm);
    btn_web_Start->setFixedWidth(50);
    connect(btn_web_Start, &ElaToggleButton::toggled, this, &ui_display::on_btn_web_Start_clicked);

    layPort->addWidget(lblPort);
    layPort->addWidget(txt_web_Port);
    layPort->addWidget(btn_web_Start);

    aBox->addLayout(layIp);
    aBox->addLayout(layPort);

    // 添加所有 Group
    sideLayout->addWidget(grpVideo);
    sideLayout->addWidget(grpHid);
    sideLayout->addWidget(grpIpKvm);
    sideLayout->addStretch();
}
