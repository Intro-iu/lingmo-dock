/*
 * Copyright (C) 2021 LingmoOS Team.
 *
 * Author:     rekols <revenmartin@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "xwindowinterface.h"
#include "utils.h"

#include <QTimer>
#include <QDebug>
#include <QWindow>
#include <QScreen>
#include <QGuiApplication>

#include <KWindowEffects>
#include <KWindowSystem>
#include <KWindowInfo>
#include <KX11Extras>

// X11
#include <NETWM>

static XWindowInterface *INSTANCE = nullptr;

XWindowInterface *XWindowInterface::instance()
{
    if (!INSTANCE)
        INSTANCE = new XWindowInterface;

    return INSTANCE;
}

XWindowInterface::XWindowInterface(QObject *parent)
    : QObject(parent)
{
    connect(KX11Extras::self(), &KX11Extras::windowAdded, this, &XWindowInterface::onWindowadded);
    connect(KX11Extras::self(), &KX11Extras::windowRemoved, this, &XWindowInterface::windowRemoved);
    connect(KX11Extras::self(), &KX11Extras::activeWindowChanged, this, &XWindowInterface::activeChanged);
}

void XWindowInterface::enableBlurBehind(QWindow *view, bool enable, const QRegion &region)
{
    KWindowEffects::enableBlurBehind(view->winId(), enable, region);
}

WId XWindowInterface::activeWindow()
{
    return KX11Extras::activeWindow();
}

void XWindowInterface::minimizeWindow(WId win)
{
    KX11Extras::minimizeWindow(win);
}

void XWindowInterface::closeWindow(WId id)
{
    // FIXME: Why there is no such thing in KWindowSystem??
    if (auto *native = dynamic_cast<QNativeInterface::QX11Application *>(qApp)) {
        NETRootInfo(native->connection(), NET::CloseWindow).closeWindowRequest(id);
    }
}

void XWindowInterface::forceActiveWindow(WId win)
{
    KWindowSystem::forceActiveWindow(win);
}

QMap<QString, QVariant> XWindowInterface::requestInfo(quint64 wid)
{
    const KWindowInfo winfo { wid, NET::WMFrameExtents
                | NET::WMWindowType
                | NET::WMGeometry
                | NET::WMDesktop
                | NET::WMState
                | NET::WMName
                | NET::WMVisibleName,
                NET::WM2WindowClass
                | NET::WM2Activities
                | NET::WM2AllowedActions
                | NET::WM2TransientFor };
    QMap<QString, QVariant> result;
    const QString winClass = QString(winfo.windowClassClass());

    result.insert("iconName", winClass.toLower());
    result.insert("active", wid == KWindowSystem::activeWindow());
    result.insert("visibleName", winfo.visibleName());
    result.insert("id", winClass);

    return result;
}

QString XWindowInterface::requestWindowClass(quint64 wid)
{
    return KWindowInfo(wid, NET::Supported, NET::WM2WindowClass).windowClassClass();
}

bool XWindowInterface::isAcceptableWindow(quint64 wid)
{
    QFlags<NET::WindowTypeMask> ignoreList;
    ignoreList |= NET::DesktopMask;
    ignoreList |= NET::DockMask;
    ignoreList |= NET::SplashMask;
    ignoreList |= NET::ToolbarMask;
    ignoreList |= NET::MenuMask;
    ignoreList |= NET::PopupMenuMask;
    ignoreList |= NET::NotificationMask;

    KWindowInfo info(wid, NET::WMWindowType | NET::WMState, NET::WM2TransientFor | NET::WM2WindowClass);

    if (!info.valid())
        return false;

    if (NET::typeMatchesMask(info.windowType(NET::AllTypesMask), ignoreList))
        return false;

    if (info.hasState(NET::SkipTaskbar) || info.hasState(NET::SkipPager))
        return false;

    // WM_TRANSIENT_FOR hint not set - normal window
    WId transFor = info.transientFor();
    const xcb_setup_t *setup = xcb_get_setup(connection);
    xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
    xcb_screen_t *screen = iter.data;
    xcb_window_t rootWindow = screen->root;

    if (transFor == 0 || transFor == wid || transFor == (WId) rootWindow)
        return true;

    info = KWindowInfo(transFor, NET::WMWindowType);

    QFlags<NET::WindowTypeMask> normalFlag;
    normalFlag |= NET::NormalMask;
    normalFlag |= NET::DialogMask;
    normalFlag |= NET::UtilityMask;

    return !NET::typeMatchesMask(info.windowType(NET::AllTypesMask), normalFlag);
}

void XWindowInterface::setViewStruts(QWindow *view, DockSettings::Direction direction, const QRect &rect, bool compositing)
{
    NETExtendedStrut strut;

    const auto screen = view->screen();

    // const QRect currentScreen {screen->geometry()};
    const QRect wholeScreen { {0, 0}, screen->virtualSize() };
    bool isRound = DockSettings::self()->style() == DockSettings::Round;
    const int edgeMargins = compositing && isRound? DockSettings::self()->edgeMargins() : 0;

    switch (direction) {
    case DockSettings::Left: {
        const int leftOffset = { screen->geometry().left() };
        strut.left_width = rect.width() + leftOffset + edgeMargins;
        strut.left_start = rect.y();
        strut.left_end = rect.y() + rect.height() - 1;
        break;
    }
    case DockSettings::Bottom: {
        strut.bottom_width = rect.height() + edgeMargins;
        strut.bottom_start = rect.x();
        strut.bottom_end = rect.x() + rect.width();
        break;
    }
    case DockSettings::Right: {
        // const int rightOffset = {wholeScreen.right() - currentScreen.right()};
        strut.right_width = rect.width() + edgeMargins;
        strut.right_start = rect.y();
        strut.right_end = rect.y() + rect.height() - 1;
        break;
    }
    default:
        break;
    }

    KWindowSystem::setExtendedStrut(view->winId(),
                                    strut.left_width,   strut.left_start,   strut.left_end,
                                    strut.right_width,  strut.right_start,  strut.right_end,
                                    strut.top_width,    strut.top_start,    strut.top_end,
                                    strut.bottom_width, strut.bottom_start, strut.bottom_end
                                    );
}

void XWindowInterface::clearViewStruts(QWindow *view)
{
    KWindowSystem::setExtendedStrut(view->winId(), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

void XWindowInterface::startInitWindows()
{
    for (auto wid : KWindowSystem::self()->windows()) {
        onWindowadded(wid);
    }
}

QString XWindowInterface::desktopFilePath(quint64 wid)
{
    const KWindowInfo info(wid, NET::Properties(), NET::WM2WindowClass | NET::WM2DesktopFileName);
    if (auto *native = dynamic_cast<QNativeInterface::QX11Application *>(qApp)){
        const xcb_setup_t *setup = xcb_get_setup(connection);
        xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
        xcb_screen_t *screen = iter.data;

        return Utils::instance()->desktopPathFromMetadata(info.windowClassClass(),
                                                      NETWinInfo(native->connction(), wid,
                                                                 screen->root,
                                                                 NET::WMPid,
                                                                 NET::Properties2()).pid(),
                                                      info.windowClassName());
    }
}

void XWindowInterface::setIconGeometry(quint64 wid, const QRect &rect)
{
    if (auto *native = dynamic_cast<QNativeInterface::QX11Application *>(qApp)) {
        const xcb_setup_t *setup = xcb_get_setup(connection);
        xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
        xcb_screen_t *screen = iter.data;

        NETWinInfo info(native->connection(),
                        wid,
                        (WId) screen->root,
                        NET::WMIconGeometry,
                        QFlags<NET::Property2>(1));
        NETRect nrect;
        nrect.pos.x = rect.x();
        nrect.pos.y = rect.y();
        nrect.size.height = rect.height();
        nrect.size.width = rect.width();
        info.setIconGeometry(nrect);
    }
}

void XWindowInterface::onWindowadded(quint64 wid)
{
    if (isAcceptableWindow(wid)) {
        emit windowAdded(wid);
    }
}
