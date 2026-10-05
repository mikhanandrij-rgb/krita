# Карта кода Krita для мобильного форка

Пути — от корня репозитория. Отмечено, что важно для режима телефона.

## Сборка и упаковка Android
| Путь | Что |
|---|---|
| `packaging/android/apk/` | Gradle-проект APK: `AndroidManifest.xml`, `build.gradle` (AGP 8.12, NDK 27.3, SDK 36, minSdk 24), Java-код |
| `packaging/android/apk/src/org/krita/android/` | `MainActivity` (полноэкранный режим, splash), `DocumentSaverService` (дописывает файл в фоне), `VideoEncoder` (экспорт анимации через MediaCodec), `ScalingDialog` (масштаб интерфейса), пожертвования через Play Billing |
| `build-tools/ci-scripts/android.yml`, `build-android-package.py` | Upstream CI (GitLab): готовые зависимости из реестра KDE → сборка Krita → `create-apk` → Gradle |
| `CMakeLists.txt` (стр. ~300, 1124, 1524, 1722) | Android-ветки: STL, суффиксы библиотек, FFmpeg через pkg-config, `create-apk` |
| `krita/CMakeLists.txt` | `libkrita.so` вместо исполняемого файла, версия `krita5.xmlgui` для Android |
| `krita/kritamenu_android.action` | Android-специфичные пункты меню (масштаб, поддержка) |

## Главное окно и интерфейс
| Путь | Что |
|---|---|
| `libs/ui/KisMainWindow.{h,cpp}` | Окно: `QStackedWidget` (стартовая страница / `QMdiArea` с видами), создание всех докеров (`createDockWidget`), меню из `krita5.xmlgui`, `openCommandBar()` (поиск по действиям), Android-хаки (фиксированный размер окна, поворот, перерисовка) |
| `libs/ui/KisViewManager.cpp` | Менеджеры действий, слоёв, выделений, фильтров, ресурсов холста; верхняя панель параметров кисти |
| `libs/ui/KisWelcomePageWidget.cpp` | Стартовая страница (недавние файлы, новости) — будет заменена хабом в режиме телефона |
| `libs/ui/KisPart.cpp` | Документы, окна, недавние файлы |
| `libs/ui/kis_action_manager.cpp`, `kis_action_registry.cpp` | Регистрация действий из `.action`-файлов, условия активности |
| `libs/widgetutils/xmlgui/` | Встроенная копия KXMLGUI (меню/панели, KisKActionCollection) |
| `libs/ui/KisAndroidScaling.cpp` | Масштаб интерфейса на Android (патч Qt «density adjustment») |
| `libs/ui/KisAutoSaveRecoveryDialog.cpp` | Восстановление автосохранений |
| `krita/krita5.xmlgui` | Структура главного меню и панелей |

## Докеры, панели, инструменты
| Путь | Что |
|---|---|
| `libs/flake/KoDockFactoryBase.h`, `KoDockRegistry` | Фабрики докеров (29 в `plugins/dockers/` + панель инструментов + параметры инструмента) |
| `plugins/dockers/layerdocker/` | Слои (модель `KisNodeModel`, делегат с миниатюрами) |
| `plugins/dockers/presetdocker/`, `libs/ui/widgets/kis_paintop_box.cpp` | Пресеты кистей и редактор кисти (`KisPaintOpPresetsEditor`) |
| `plugins/dockers/animation/` | Таймлайн, кривые, луковая кожа |
| `plugins/dockers/advancedcolorselector/`, `widegamutcolorselector/`, `palettedocker/` | Цвет |
| `plugins/dockers/textproperties/` | QML-докер свойств текста (пример QML внутри виджетов) |
| `plugins/dockers/touchdocker/` | Старый «сенсорный» докер на QML — кнопки действий |
| `libs/flake/KoToolManager`, `KoToolBox`, `KoToolDocker` | Инструменты, панель инструментов, параметры инструмента |
| `plugins/tools/` | 35 инструментов (кисти, выделения, трансформация, текст, заливка, …) |
| `plugins/assistants/` | Помощники рисования |
| `qmlmodules/` | QML-компоненты Krita и `KisQQuickWidget` — QML внутри виджетов |

## Холст и ввод
| Путь | Что |
|---|---|
| `libs/ui/canvas/kis_canvas2.cpp` | Холст, связь с изображением |
| `libs/ui/opengl/` | OpenGL/GLES-холст (`KisOpenGLCanvas2`, текстуры тайлов, шейдеры), `KisOpenGLModeProber` |
| `libs/ui/input/kis_input_manager*.cpp`, `kis_shortcut_matcher.cpp` | Ввод: мышь, планшет, касания, жесты; профили в `krita/data/input/*.profile` (в т. ч. секция «Touch Gestures») |
| `libs/ui/input/kis_touch_shortcut.cpp`, `KisTouchGestureAction` | Жесты: касание 2/3 пальцами (отмена/повтор), щипок, поворот |
| `libs/ui/KisLongPressEventFilter.cpp` | Долгое нажатие = правый клик (Android) |
| `libs/ui/kis_config.cpp` | Настройки: палм-реджект/«рисовать пальцем» (`disableTouchOnCanvas`, `touchPainting`), режим холста и т. п. |

## Ресурсы, память, плагины
| Путь | Что |
|---|---|
| `libs/resources/` | База ресурсов SQLite (`KisResourceCacheDb`), загрузка бандлов при запуске — главный кандидат на ускорение старта |
| `libs/image/tiles3/` | Тайловая память, `swap/` — свап на диск |
| `libs/image/kis_image_config.cpp` | Лимиты памяти (`memoryHardLimitPercent`, `memorySoftLimitPercent`, `maxSwapSize`), кэш кадров анимации |
| `libs/ui/kis_config.cpp` | `undoStackLimit`, качество превью, режим OpenGL |
| `libs/koplugin/KoPluginLoader.cpp`, `KoJsonTrader.cpp` | Загрузка плагинов (на Android — `lib_krita*.so` из APK) |
| `plugins/extensions/pykrita/` | Python-скрипты (на Android не собирается) |
| `plugins/impex/` | 41 фильтр файлов (kra, psd, ora, png, jpeg, webp, jxl, heif, exr, tiff, svg, gif, pdf, …) |

## Что использовать повторно в мобильном режиме
- `KisMainWindow::openCommandBar()` / `KisCommandBar` — основа поиска команд.
- `guiFactory()->containers("Menu")` — живые меню для групп команд.
- Модели данных докеров (`KisNodeModel`, `KisPresetChooser`/`KisResourceModel`,
  модели таймлайна) — для мобильных карточек без дублирования логики.
- Сенсорные жесты Krita — настраиваются профилем ввода, не переписываются.
- `KisAndroidUtils::setFullScreen`, `KisAndroidScaling` — системные панели и масштаб.
