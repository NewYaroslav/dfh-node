# AGENTS

## Сборка и тесты (Windows: MSVC + MinGW)

Конфигурация build-msvc (одной командой):

```
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
```

Запуск тестов под MinGW:

```
build-tests-mingw.bat
```

Запуск тестов под MSVC (Debug) и затем MinGW:

```
run-all-tests.bat
```

Примечание: папки build-* и артефакты IDE в репозиторий не коммитятся.
