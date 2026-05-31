#!/bin/bash

# Symmetra Docker Services Startup Script

set -e

echo "🚀 Starting Symmetra Services..."

# Check if .env exists
if [ ! -f .env ]; then
    echo "📝 Creating .env from .env.example..."
    cp .env.example .env
    echo "✅ .env created. Edit it if needed."
fi

# Check Docker
if ! command -v docker &> /dev/null; then
    echo "❌ Docker is not installed. Please install Docker first."
    exit 1
fi

if ! command -v docker-compose &> /dev/null; then
    echo "❌ Docker Compose is not installed. Please install Docker Compose first."
    exit 1
fi

# Start services
echo "🐳 Starting Docker containers..."
docker-compose up -d

# Wait for services to be ready
echo "⏳ Waiting for services to be ready..."
sleep 5

# Check services
echo "📊 Checking service status..."
docker-compose ps

# Wait for health checks
echo "🏥 Waiting for health checks..."

# PostgreSQL
echo -n "  ⏳ PostgreSQL... "
for i in {1..30}; do
    if docker-compose exec postgres pg_isready -U postgres &> /dev/null; then
        echo "✅"
        break
    fi
    if [ $i -eq 30 ]; then
        echo "❌"
    fi
    sleep 1
done

# MongoDB
echo -n "  ⏳ MongoDB... "
for i in {1..30}; do
    if docker-compose exec mongodb mongosh --eval "db.runCommand('ping')" &> /dev/null; then
        echo "✅"
        break
    fi
    if [ $i -eq 30 ]; then
        echo "❌"
    fi
    sleep 1
done

# Kafka
echo -n "  ⏳ Kafka... "
for i in {1..30}; do
    if docker-compose exec kafka kafka-broker-api-versions --bootstrap-server localhost:9092 &> /dev/null; then
        echo "✅"
        break
    fi
    if [ $i -eq 30 ]; then
        echo "❌"
    fi
    sleep 1
done

# RabbitMQ
echo -n "  ⏳ RabbitMQ... "
for i in {1..30}; do
    if docker-compose exec rabbitmq rabbitmq-diagnostics -q ping &> /dev/null; then
        echo "✅"
        break
    fi
    if [ $i -eq 30 ]; then
        echo "❌"
    fi
    sleep 1
done

# Print access information
echo ""
echo "════════════════════════════════════════════════════════════════"
echo "✅ All services are running!"
echo "════════════════════════════════════════════════════════════════"
echo ""
echo "📊 Service URLs:"
echo "  🐘 PostgreSQL:        postgres://postgres:postgres@localhost:5432/symmetra"
echo "  🍃 MongoDB:           mongodb://admin:mongo123@localhost:27017/symmetra"
echo "  🚀 Kafka:             localhost:9093"
echo "  📡 Kafka UI:          http://localhost:8080"
echo "  🐰 RabbitMQ:          amqp://guest:guest@localhost:5672"
echo "  🐰 RabbitMQ UI:       http://localhost:15672 (guest:guest)"
echo ""
echo "🌐 Application APIs:"
echo "  📝 MongoDB CRUD:      http://localhost:3000"
echo "  📤 Kafka Producer:    http://localhost:3001"
echo "  📨 RabbitMQ Producer: http://localhost:3002"
echo ""
echo "📖 Documentation:"
echo "  🐳 Docker Setup:      See DOCKER_SETUP.md"
echo "  🧪 Testing Guide:     See TESTING_GUIDE.md"
echo ""
echo "💡 Quick Tests:"
echo ""
echo "  MongoDB CRUD:"
echo "    curl -X POST http://localhost:3000/users \\"
echo "      -H 'Content-Type: application/json' \\"
echo "      -d '{\"name\":\"Alice\",\"email\":\"alice@example.com\",\"age\":30}'"
echo ""
echo "  Kafka Producer:"
echo "    curl -X POST http://localhost:3001/events/user \\"
echo "      -H 'Content-Type: application/json' \\"
echo "      -d '{\"userId\":\"123\",\"action\":\"created\",\"data\":{}}'"
echo ""
echo "  RabbitMQ Producer:"
echo "    curl -X POST http://localhost:3002/queue/email \\"
echo "      -H 'Content-Type: application/json' \\"
echo "      -d '{\"to\":\"user@example.com\",\"subject\":\"Hello\"}'"
echo ""
echo "════════════════════════════════════════════════════════════════"
echo ""
echo "To view logs:"
echo "  docker-compose logs -f"
echo ""
echo "To stop services:"
echo "  docker-compose down"
echo ""
echo "To stop and remove volumes:"
echo "  docker-compose down -v"
echo ""
