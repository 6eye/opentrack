/* Copyright (c) 2013-2015, Stanislaw Halik <sthalik@misaki.pl>

 * Permission to use, copy, modify, and/or distribute this
 * software for any purpose with or without fee is hereby granted,
 * provided that the above copyright notice and this permission
 * notice appear in all copies.
 */

#include "ui.h"
#include "opentrack/tracker.h"
#include "tracker-pt/ftnoir_tracker_pt.h"
#include "filter-accela/ftnoir_filter_accela.h"
#include "opentrack-compat/options.hpp"
#include "new_file_dialog.h"
#include "wizard.h"
#include "opentrack-compat/sleep.hpp"
#include <QFileDialog>
#include <QDesktopServices>
#include <QProcess>
#include <QDebug>

#ifndef _WIN32
#   include <unistd.h>
#else
#   include <windows.h>
#endif

MainWindow::MainWindow() :
    pose_update_timer(this),
    kbd_quit(QKeySequence("Ctrl+Q"), this),
    is_refreshing_profiles(false),
    keys_paused(false),
    update_query(this)
{
    ui.setupUi(this);

    setFixedSize(size());

    updateButtonState(false, false);

    connect(ui.btnEditCurves, SIGNAL(clicked()), this, SLOT(showCurveConfiguration()));
    connect(ui.btnShortcuts, SIGNAL(clicked()), this, SLOT(show_options_dialog()));
    connect(ui.btnShowServerControls, SIGNAL(clicked()), this, SLOT(showProtocolSettings()));

    for (auto x : modules.protocols())
        ui.iconcomboProtocol->addItem(x->icon, x->name);

    refresh_config_list();
    connect(&config_list_timer, SIGNAL(timeout()), this, SLOT(refresh_config_list()));
    config_list_timer.start(1000 * 3);

    tie_setting(s.protocol_dll, ui.iconcomboProtocol);

    connect(ui.iconcomboProtocol,
            &QComboBox::currentTextChanged,
            [&](QString) -> void { if (pProtocolDialog) pProtocolDialog = nullptr; save(); });

    connect(ui.btnStartTracker, SIGNAL(clicked()), this, SLOT(startTracker()));
    connect(ui.btnStopTracker, SIGNAL(clicked()), this, SLOT(stopTracker()));
    connect(ui.iconcomboProfile, SIGNAL(currentTextChanged(QString)), this, SLOT(profileSelected(QString)));

    connect(&pose_update_timer, SIGNAL(timeout()), this, SLOT(showHeadPose()));
    connect(&kbd_quit, SIGNAL(activated()), this, SLOT(exit()));

    save_timer.setSingleShot(true);
    connect(&save_timer, SIGNAL(timeout()), this, SLOT(_save()));

    profile_menu.addAction("Create new empty config", this, SLOT(make_empty_config()));
    profile_menu.addAction("Create new copied config", this, SLOT(make_copied_config()));
    profile_menu.addAction("Open configuration directory", this, SLOT(open_config_directory()));
    ui.profile_button->setMenu(&profile_menu);

    kbd_quit.setEnabled(true);

    connect(&det_timer, SIGNAL(timeout()), this, SLOT(maybe_start_profile_from_executable()));
    det_timer.start(1000);

    ensure_tray();
    set_working_directory();

    if (!QFile(group::ini_pathname()).exists())
    {
        set_profile(OPENTRACK_DEFAULT_CONFIG);
        const auto pathname = group::ini_pathname();
        if (!QFile(pathname).exists())
        {
            QFile file(pathname);
            (void) file.open(QFile::ReadWrite);
        }
    }

    if (group::ini_directory() == "")
        QMessageBox::warning(this,
                             "Configuration not saved.",
                             "Can't create configuration directory! Expect major malfunction.",
                             QMessageBox::Ok, QMessageBox::NoButton);

    connect(this, &MainWindow::emit_start_tracker,
            this, [&]() -> void { if (keys_paused) return; qDebug() << "start tracker"; startTracker(); },
            Qt::QueuedConnection);

    connect(this, &MainWindow::emit_stop_tracker,
            this, [&]() -> void { if (keys_paused) return; qDebug() << "stop tracker"; stopTracker(); },
            Qt::QueuedConnection);

    connect(this, &MainWindow::emit_toggle_tracker,
            this, [&]() -> void { if (keys_paused) return; qDebug() << "toggle tracker"; if (work) stopTracker(); else startTracker(); },
            Qt::QueuedConnection);

    register_shortcuts();

    connect(this, &MainWindow::emit_minimized, this, &MainWindow::mark_minimized, Qt::QueuedConnection);

    ui.btnStartTracker->setFocus();

    update_query.maybe_show_dialog();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (maybe_not_close_tracking())
        e->ignore();
    else
        e->accept();
}

void MainWindow::register_shortcuts()
{
    using t_shortcut = std::tuple<key_opts&, Shortcuts::fun>;

    std::vector<t_shortcut> keys {
        t_shortcut(s.key_start_tracking, [&]() -> void { emit_start_tracker(); }),
        t_shortcut(s.key_stop_tracking, [&]() -> void { emit_stop_tracker(); }),
        t_shortcut(s.key_toggle_tracking, [&]() -> void { emit_toggle_tracker(); }),
    };

    global_shortcuts.reload(keys);

    if (work)
        work->reload_shortcuts();
}

bool MainWindow::get_new_config_name_from_dialog(QString& ret)
{
    new_file_dialog dlg;
    dlg.exec();
    return dlg.is_ok(ret);
}

MainWindow::~MainWindow()
{
    maybe_save();

    if (tray)
        tray->hide();
    stopTracker();
}

void MainWindow::set_working_directory()
{
    QDir::setCurrent(QCoreApplication::applicationDirPath());
}

void MainWindow::save_mappings() {
    pose.save_mappings();
}

void MainWindow::save()
{
    save_timer.stop();
    save_timer.start(5000);
}

void MainWindow::maybe_save()
{
    if (save_timer.isActive())
    {
        save_timer.stop();
        _save();
    }
}

void MainWindow::_save() {
    s.b->save();
    save_mappings();
    mem<QSettings> settings = group::ini_file();
    settings->sync();

#if defined(__unix) || defined(__linux)
    QString currentFile = group::ini_pathname();
    QByteArray bytes = QFile::encodeName(currentFile);
    const char* filename_as_asciiz = bytes.constData();

    if (access(filename_as_asciiz, R_OK | W_OK))
    {
        QMessageBox::warning(this, "Something went wrong", "Check permissions and ownership for your .ini file!", QMessageBox::Ok, QMessageBox::NoButton);
    }
#endif
}

void MainWindow::load_mappings() {
    pose.load_mappings();
}

void MainWindow::load_settings() {
    s.b->reload();
    load_mappings();
}

void MainWindow::make_empty_config()
{
    QString name;
    const QString dir = group::ini_directory();
    const QString old_name = group::ini_filename();

    if (dir != "" && get_new_config_name_from_dialog(name))
    {
        // don't create the file until wizard is done. only create the registry entry.
        save();
        QSettings(OPENTRACK_ORG).setValue(OPENTRACK_CONFIG_FILENAME_KEY, name);

        const int code = Wizard(this).exec();

        if (code == QWizard::Accepted)
        {
            QFile(dir + "/" + name).open(QFile::ReadWrite);
            refresh_config_list();
            ui.iconcomboProfile->setCurrentText(name);
        }
        else
            QSettings(OPENTRACK_ORG).setValue(OPENTRACK_CONFIG_FILENAME_KEY, old_name);
    }
}

void MainWindow::make_copied_config()
{
    const QString dir = group::ini_directory();
    const QString cur = group::ini_pathname();
    QString name;
    if (cur != "" && dir != "" && get_new_config_name_from_dialog(name))
    {
        const QString new_name = dir + "/" + name;
        (void) QFile::remove(new_name);
        (void) QFile::copy(cur, new_name);
        refresh_config_list();
        ui.iconcomboProfile->setCurrentText(name);
    }
}

void MainWindow::open_config_directory()
{
    const QString path = group::ini_directory();
    if (path != "")
    {
        QDesktopServices::openUrl("file:///" + QDir::toNativeSeparators(path));
    }
}

extern "C" const char* opentrack_version;

void MainWindow::refresh_config_list()
{
    if (work)
        return;

    if (group::ini_list().size() == 0)
    {
        QFile filename(group::ini_directory() + "/" OPENTRACK_DEFAULT_CONFIG);
        (void) filename.open(QFile::ReadWrite);
    }

     QStringList ini_list = group::ini_list();
     set_title();
     QString current = group::ini_filename();
     is_refreshing_profiles = true;
     ui.iconcomboProfile->clear();
     for (auto x : ini_list)
         ui.iconcomboProfile->addItem(QIcon(":/images/settings16.png"), x);
     is_refreshing_profiles = false;
     ui.iconcomboProfile->setCurrentText(current);
}

void MainWindow::updateButtonState(bool running, bool inertialp)
{
    bool not_running = !running;
    ui.iconcomboProfile->setEnabled ( not_running );
    ui.btnStartTracker->setEnabled ( not_running );
    ui.btnStopTracker->setEnabled ( running );
    ui.iconcomboProtocol->setEnabled ( not_running );
    ui.video_frame_label->setVisible(not_running || inertialp);
    ui.profile_button->setEnabled(not_running);
}

void MainWindow::reload_options()
{
    if (work)
        work->reload_shortcuts();
    ensure_tray();
}

void MainWindow::startTracker() {
    if (work)
        return;

    // tracker dtor needs run first
    work = nullptr;

    {
        int status = QProcess::execute("taskkill -f -im cl-eyetest.exe");
        if (!status)
        {
            qDebug() << "trackhat: killed cl-eye test";
            portable::sleep(1000);
        }
    }

    libs = SelectedLibraries(ui.video_frame, std::make_shared<Tracker_PT>(), current_protocol(), std::make_shared<FTNoIR_Filter>());

    {
        double p[6] = {0,0,0, 0,0,0};
        display_pose(p, p);
    }

    if (!libs.correct)
    {
        QMessageBox::warning(this, "Library load error",
                             "One of libraries failed to load. Check installation.",
                             QMessageBox::Ok,
                             QMessageBox::NoButton);
        libs = SelectedLibraries();
        return;
    }

    work = std::make_shared<Work>(s, pose, libs, winId());

    reload_options();

    if (pProtocolDialog)
        pProtocolDialog->register_protocol(libs.pProtocol.get());

    pose_update_timer.start(50);

    // NB check valid since SelectedLibraries ctor called
    // trackers take care of layout state updates
    const bool is_inertial = ui.video_frame->layout() == nullptr;
    updateButtonState(true, is_inertial);

    maybe_save();

    ui.btnStopTracker->setFocus();
}

void MainWindow::stopTracker() {
    if (!work)
        return;

    //ui.game_name->setText("Not connected");

    pose_update_timer.stop();
    ui.pose_display->rotateBy(0, 0, 0, 0, 0, 0);

    if (pProtocolDialog)
        pProtocolDialog->unregister_protocol();

    work = nullptr;
    libs = SelectedLibraries();

    {
        double p[6] = {0,0,0, 0,0,0};
        display_pose(p, p);
    }
    updateButtonState(false, false);

    set_title();

    ui.btnStartTracker->setFocus();
}

void MainWindow::display_pose(const double *mapped, const double *raw)
{
    ui.pose_display->rotateBy(mapped[Yaw], -mapped[Pitch], -mapped[Roll],
                              mapped[TX], mapped[TY], mapped[TZ]);

    if (mapping_widget)
        mapping_widget->update();

    double raw_[6];

    for (int i = 0; i < 6; i++)
    {
        raw_[i] = (int) raw[i];
    }

    ui.raw_x->display(raw_[TX]);
    ui.raw_y->display(raw_[TY]);
    ui.raw_z->display(raw_[TZ]);
    ui.raw_yaw->display(raw_[Yaw]);
    ui.raw_pitch->display(raw_[Pitch]);
    ui.raw_roll->display(raw_[Roll]);

    QString game_title;
    if (libs.pProtocol)
        game_title = libs.pProtocol->game_name();
    set_title(game_title);
}

void MainWindow::set_title(const QString& game_title_)
{
    QString game_title;
    if (game_title_ != "")
        game_title = " :: " + game_title_;
    QString current = group::ini_filename();
    setWindowTitle(opentrack_version + QStringLiteral(" opentrack") + QStringLiteral(" :: ") + current + game_title);
}

void MainWindow::showHeadPose()
{
    if (!ui.video_frame->isEnabled())
        return;

    double mapped[6], raw[6];

    work->tracker->get_raw_and_mapped_poses(mapped, raw);

    display_pose(mapped, raw);
}

template<typename t>
bool mk_dialog(mem<dylib> lib, mem<t>* orig)
{
    if (*orig && (*orig)->isVisible())
    {
        (*orig)->show();
        (*orig)->raise();
        return false;
    }

    if (lib && lib->Dialog)
    {
        auto dialog = mem<t>(reinterpret_cast<t*>(lib->Dialog()));
        dialog->setWindowFlags(Qt::Dialog);
        dialog->setFixedSize(dialog->size());

        *orig = dialog;
        dialog->show();

        return true;
    }

    return false;
}
void MainWindow::showProtocolSettings() {
    if (mk_dialog(current_protocol(), &pProtocolDialog) && libs.pProtocol)
        pProtocolDialog->register_protocol(libs.pProtocol.get());
}
template<typename t, typename... Args>
bool mk_window(mem<t>* place, Args&&... params)
{
    if (*place && (*place)->isVisible())
    {
        (*place)->show();
        (*place)->raise();
        return false;
    }
    else
    {
        *place = std::make_shared<t>(std::forward<Args>(params)...);
        (*place)->setWindowFlags(Qt::Dialog);
        (*place)->show();
        return true;
    }
}

void MainWindow::show_options_dialog() {
    if (mk_window(&options_widget,
                  s,
                  *this,
                  [&]() -> void { register_shortcuts(); },
                  [&](bool flag) -> void { keys_paused = flag; }))
        connect(options_widget.get(), SIGNAL(reload()), this, SLOT(reload_options()));
}

void MainWindow::showCurveConfiguration() {
    mk_window(&mapping_widget, pose, s);
}

bool MainWindow::maybe_not_close_tracking()
{
    if (work != nullptr)
    {
        auto btn = QMessageBox::warning(this, "Tracking active",
                                        "Are you sure you want to exit? This will terminate tracking.",
                                        QMessageBox::Yes, QMessageBox::No);
        if (btn == QMessageBox::No)
            return true;
    }
    return false;
}

void MainWindow::exit() {

    if (!maybe_not_close_tracking())
        QCoreApplication::exit(0);
}

void MainWindow::profileSelected(QString name)
{
    if (name == "" || is_refreshing_profiles)
        return;

    const auto old_name = group::ini_filename();
    const auto new_name = name;

    if (old_name != new_name)
    {
        save_timer.stop();
        _save();

        {
            QSettings settings(OPENTRACK_ORG);
            settings.setValue (OPENTRACK_CONFIG_FILENAME_KEY, new_name);
        }

        set_title();
        load_settings();
    }
}

void MainWindow::ensure_tray()
{
    if (tray)
        tray->hide();
    tray = nullptr;
    if (s.tray_enabled)
    {
        tray = std::make_shared<QSystemTrayIcon>(this);
        tray->setIcon(QIcon(":/images/facetracknoir.png"));
        tray->show();
        connect(tray.get(), SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
                this, SLOT(restore_from_tray(QSystemTrayIcon::ActivationReason)));
    }
}

void MainWindow::restore_from_tray(QSystemTrayIcon::ActivationReason)
{
    show();
    setWindowState(Qt::WindowNoState);
}

void MainWindow::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::WindowStateChange)
    {
        const bool is_minimized = windowState() & Qt::WindowMinimized;

        if (s.tray_enabled && is_minimized)
        {
            if (!tray)
                ensure_tray();
            hide();
        }

        emit_minimized(is_minimized);
    }

    QMainWindow::changeEvent(e);
}

void MainWindow::mark_minimized(bool is_minimized)
{
    ui.video_frame->setEnabled(!is_minimized);
}

void MainWindow::maybe_start_profile_from_executable()
{
    if (!work)
    {
        QString prof;
        if (det.config_to_start(prof))
        {
            ui.iconcomboProfile->setCurrentText(prof);
            startTracker();
        }
    }
    else
    {
        if (det.should_stop())
            stopTracker();
    }
}

void MainWindow::set_profile(const QString &profile)
{
    QSettings settings(OPENTRACK_ORG);
    settings.setValue(OPENTRACK_CONFIG_FILENAME_KEY, profile);
}
