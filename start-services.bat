@echo off
REM Symmetra Docker Services Startup Script (Windows)

setlocal enabledelayedexpansion

echo.
echo 🚀 Starting Symmetra Services...
echo.

REM Check if .env exists
if not exist .env (
    echo 📝 Creating .env from .env.example...
    copy .env.example .env
    echo ✅ .env created. Edit it if needed.
    echo.
)

REM Check Docker
where docker >nul 2>nul
if errorlevel 1 (
    echo ❌ Docker is not installed. Please install Docker Desktop for Windows.
    pause
    exit /b 1
)

REM Start services
echo 🐳 Starting Docker containers...
docker-compose up -d

if errorlevel 1 (
    echo ❌ Failed to start Docker services.
    pause
    exit /b 1
)

echo.
echo ⏳ Waiting for services to be ready...
timeout /t 5 /nobreak

REM Check services
echo.
echo 📊 Checking service status...
docker-compose ps

echo.
echo 🏥 Waiting for health checks...

REM PostgreSQL
echo   ⏳ PostgreSQL...
for /l %%i in (1,1,30) do (
    docker-compose exec postgres pg_isready -U postgres >nul 2>nul
    if not errorlevel 1 (
        echo   ✅ PostgreSQL ready
        goto postgresql_done
    )
    timeout /t 1 /nobreak >nul
)
echo   ❌ PostgreSQL failed to start
:postgresql_done

REM MongoDB
echo   ⏳ MongoDB...
for /l %%i in (1,1,30) do (
    docker-compose exec mongodb mongosh --eval "db.runCommand('ping')" >nul 2>nul
    if not errorlevel 1 (
        echo   ✅ MongoDB ready
        goto mongodb_done
    )
    timeout /t 1 /nobreak >nul
)
echo   ❌ MongoDB failed to start
:mongodb_done

REM Kafka
echo   ⏳ Kafka...
for /l %%i in (1,1,30) do (
    docker-compose exec kafka kafka-broker-api-versions --bootstrap-server localhost:9092 >nul 2>nul
    if not errorlevel 1 (
        echo   ✅ Kafka ready
        goto kafka_done
    )
    timeout /t 1 /nobreak >nul
)
echo   ❌ Kafka failed to start
:kafka_done

REM RabbitMQ
echo   ⏳ RabbitMQ...
for /l %%i in (1,1,30) do (
    docker-compose exec rabbitmq rabbitmq-diagnostics -q ping >nul 2>nul
    if not errorlevel 1 (
        echo   ✅ RabbitMQ ready
        goto rabbitmq_done
    )
    timeout /t 1 /nobreak >nul
)
echo   ❌ RabbitMQ failed to start
:rabbitmq_done

REM Print access information
echo.
echo ════════════════════════════════════════════════════════════════
echo ✅ All services are running!
echo ════════════════════════════════════════════════════════════════
echo.
echo 📊 Service URLs:
echo   🐘 PostgreSQL:        postgres://postgres:postgres@localhost:5432/symmetra
echo   🍃 MongoDB:           mongodb://admin:mongo123@localhost:27017/symmetra
echo   🚀 Kafka:             localhost:9093
echo   📡 Kafka UI:          http://localhost:8080
echo   🐰 RabbitMQ:          amqp://guest:guest@localhost:5672
echo   🐰 RabbitMQ UI:       http://localhost:15672 (guest:guest)
echo.
echo 🌐 Application APIs:
echo   📝 MongoDB CRUD:      http://localhost:3000
echo   📤 Kafka Producer:    http://localhost:3001
echo   📨 RabbitMQ Producer: http://localhost:3002
echo.
echo 📖 Documentation:
echo   🐳 Docker Setup:      See DOCKER_SETUP.md
echo   🧪 Testing Guide:     See TESTING_GUIDE.md
echo.
echo 💡 Quick Tests:
echo.
echo   MongoDB CRUD:
echo     curl -X POST http://localhost:3000/users ^
echo       -H "Content-Type: application/json" ^
echo       -d "{\"name\":\"Alice\",\"email\":\"alice@example.com\",\"age\":30}"
echo.
echo   Kafka Producer:
echo     curl -X POST http://localhost:3001/events/user ^
echo       -H "Content-Type: application/json" ^
echo       -d "{\"userId\":\"123\",\"action\":\"created\",\"data\":{}}"
echo.
echo   RabbitMQ Producer:
echo     curl -X POST http://localhost:3002/queue/email ^
echo       -H "Content-Type: application/json" ^
echo       -d "{\"to\":\"user@example.com\",\"subject\":\"Hello\"}"
echo.
echo ════════════════════════════════════════════════════════════════
echo.
echo To view logs:
echo   docker-compose logs -f
echo.
echo To stop services:
echo   docker-compose down
echo.
echo To stop and remove volumes:
echo   docker-compose down -v
echo.

pause
