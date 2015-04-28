#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QMainWindow>
#include "ui_mainwindow.h"

#include "world.hpp"

#include <boost/optional.hpp>

namespace Ui {
class MainWindow;
}

class main_window : public QMainWindow
{
  Q_OBJECT

public:
  explicit main_window(QWidget *parent = 0);

private slots:
  void open_map();
  void tile_activate(unsigned, unsigned);

private:
  Ui::MainWindow ui_;
  boost::optional<world> world_;
};

#endif // MAINWINDOW_HPP
