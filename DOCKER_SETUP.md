# 🐳 Docker Setup Guide

Complete Docker Compose stack for Symmetra with all services: PostgreSQL, MongoDB, Kafka, RabbitMQ, and all three example applications.

---

## Quick Start

```bash
# Copy environment configuration
cp .env.example .env

# Start all services
docker-compose up -d

# Check status
docker-compose ps

# View logs
docker-compose logs -f
```

---

## Services

| Service | Port | URL | Purpose |
|---------|------|-----|---------|
| **PostgreSQL** | 5432 | `postgres://postgres:postgres@localhost:5432/symmetra` | SQL database |
| **MongoDB** | 27017 | `mongodb://admin:mongo123@localhost:27017/symmetra` | Document database |
| **Zookeeper** | 2181 | `localhost:2181` | Kafka coordination |
| **Kafka** | 9092/9093 | `localhost:9093` | Event streaming |
| **Kafka UI** | 8080 | `http://localhost:8080` | Kafka monitoring |
| **RabbitMQ** | 5672/15672 | `amqp://guest:guest@localhost:5672` | Message queue |
| **RabbitMQ UI** | 15672 | `http://localhost:15672` | RabbitMQ management |
| **MongoDB CRUD API** | 3000 | `http://localhost:3000` | User CRUD operations |
| **Kafka Producer** | 3001 | `http://localhost:3001` | Event publishing |
| **Kafka Consumer** | — | — | Event consuming |
| **RabbitMQ Producer** | 3002 | `http://localhost:3002` | Queue publishing |
| **RabbitMQ Consumer** | — | — | Queue consuming |

---

## Environment Configuration

Edit `.env` to customize:

```env
# PostgreSQL
POSTGRES_USER=postgres
POSTGRES_PASSWORD=postgres
POSTGRES_DB=symmetra

# MongoDB
MONGO_USER=admin
MONGO_PASSWORD=mongo123
MONGO_DB=symmetra

# RabbitMQ
RABBITMQ_USER=guest
RABBITMQ_PASSWORD=guest

# Application
NODE_ENV=development
```

---

## Usage Examples

### 1. MongoDB CRUD (Port 3000)

**Create user:**
```bash
curl -X POST http://localhost:3000/users \
  -H "Content-Type: application/json" \
  -d '{
    "name": "Alice",
    "email": "alice@example.com",
    "age": 30
  }'
```

**List users:**
```bash
curl http://localhost:3000/users
```

**Get specific user:**
```bash
curl http://localhost:3000/users/{user_id}
```

**Update user:**
```bash
curl -X PUT http://localhost:3000/users/{user_id} \
  -H "Content-Type: application/json" \
  -d '{"age": 31}'
```

**Delete user:**
```bash
curl -X DELETE http://localhost:3000/users/{user_id}
```

**Health check:**
```bash
curl http://localhost:3000/health
```

---

### 2. Kafka Producer (Port 3001)

**Publish user event:**
```bash
curl -X POST http://localhost:3001/events/user \
  -H "Content-Type: application/json" \
  -d '{
    "userId": "550e8400-e29b-41d4-a716-446655440000",
    "action": "created",
    "data": {"name": "Alice", "email": "alice@example.com"}
  }'
```

**Publish order event:**
```bash
curl -X POST http://localhost:3001/events/order \
  -H "Content-Type: application/json" \
  -d '{
    "orderId": "550e8400-e29b-41d4-a716-446655440001",
    "userId": "550e8400-e29b-41d4-a716-446655440000",
    "amount": 99.99,
    "status": "created"
  }'
```

**Batch publish:**
```bash
curl -X POST http://localhost:3001/events/batch \
  -H "Content-Type: application/json" \
  -d '{
    "events": [
      {
        "topic": "user-events",
        "key": "user-1",
        "data": {"type": "user.updated"}
      },
      {
        "topic": "order-events",
        "key": "order-1",
        "data": {"type": "order.processing"}
      }
    ]
  }'
```

**View in Kafka UI:**
```
http://localhost:8080
```

---

### 3. RabbitMQ Producer (Port 3002)

**Send email (direct exchange):**
```bash
curl -X POST http://localhost:3002/queue/email \
  -H "Content-Type: application/json" \
  -d '{
    "to": "user@example.com",
    "subject": "Welcome!",
    "template": "welcome",
    "data": {"name": "Alice"}
  }'
```

**Send notification (topic exchange):**
```bash
curl -X POST http://localhost:3002/queue/notification \
  -H "Content-Type: application/json" \
  -d '{
    "userId": "550e8400-e29b-41d4-a716-446655440000",
    "type": "login",
    "message": "Welcome back!",
    "data": {"ip": "192.168.1.1"}
  }'
```

**Queue task (fanout exchange):**
```bash
curl -X POST http://localhost:3002/queue/task \
  -H "Content-Type: application/json" \
  -d '{
    "taskId": "550e8400-e29b-41d4-a716-446655440001",
    "taskType": "export",
    "priority": "high",
    "payload": {"format": "csv", "limit": 1000}
  }'
```

**View in RabbitMQ UI:**
```
http://localhost:15672
Login: guest / guest
```

---

## Docker Compose Commands

**Start services:**
```bash
docker-compose up -d
```

**Stop services:**
```bash
docker-compose down
```

**View logs:**
```bash
docker-compose logs -f

# Specific service
docker-compose logs -f mongodb
docker-compose logs -f kafka
docker-compose logs -f symmify-mongo-crud
```

**Restart service:**
```bash
docker-compose restart mongodb
```

**Rebuild services:**
```bash
docker-compose up -d --build
```

**Clean up everything (volumes included):**
```bash
docker-compose down -v
```

---

## Monitoring

### Kafka UI
```
http://localhost:8080
- View all topics
- Monitor consumer groups
- Check message flow
- Inspect broker configuration
```

### RabbitMQ Management UI
```
http://localhost:15672
Username: guest
Password: guest

Features:
- View exchanges and queues
- Monitor message rates
- Check connection status
- Manage permissions
```

### PostgreSQL (via CLI)
```bash
docker-compose exec postgres psql -U postgres -d symmetra
```

### MongoDB (via CLI)
```bash
docker-compose exec mongodb mongosh -u admin -p mongo123 --authenticationDatabase admin
```

### Check Service Health
```bash
docker-compose ps
```

---

## Debugging

**View container logs:**
```bash
docker-compose logs symmify-mongo-crud
docker-compose logs symmify-kafka-producer
docker-compose logs symmify-kafka-consumer
docker-compose logs symmify-rabbitmq-producer
docker-compose logs symmify-rabbitmq-consumer
```

**Inspect network:**
```bash
docker network ls
docker network inspect symmetra_symmetra
```

**Test Kafka connectivity:**
```bash
docker-compose exec kafka kafka-broker-api-versions --bootstrap-server localhost:9092
```

**Test RabbitMQ connectivity:**
```bash
docker-compose exec rabbitmq rabbitmq-diagnostics -q ping
```

**Test MongoDB connectivity:**
```bash
docker-compose exec mongodb mongosh --eval "db.runCommand('ping')"
```

**Test PostgreSQL connectivity:**
```bash
docker-compose exec postgres pg_isready
```

---

## Production Considerations

### Security
- Change default passwords in `.env`
- Use Docker secrets for sensitive data
- Enable TLS/SSL for Kafka and RabbitMQ
- Restrict network access with firewall rules
- Use private Docker registries

### Performance
- Adjust resource limits in `docker-compose.yml`
- Use persistent volumes for data
- Configure connection pooling
- Monitor resource usage with `docker stats`

### Scaling
- Use Docker Swarm or Kubernetes for orchestration
- Set up load balancing for API services
- Configure replication for databases
- Use topic replication for Kafka

### Backup
```bash
# Backup MongoDB
docker-compose exec mongodb mongodump --out /backup

# Backup PostgreSQL
docker-compose exec postgres pg_dump -U postgres symmetra > backup.sql
```

---

## Troubleshooting

**Kafka not starting:**
```bash
# Check Zookeeper is healthy
docker-compose logs zookeeper

# Restart both
docker-compose restart zookeeper kafka
```

**MongoDB connection refused:**
```bash
# Check MongoDB is running
docker-compose logs mongodb

# Verify credentials in .env
```

**RabbitMQ stuck:**
```bash
# Hard reset
docker-compose down -v
docker-compose up -d rabbitmq
```

**Port already in use:**
```bash
# Find process using port
lsof -i :3000

# Change port in docker-compose.yml
# ports:
#   - "3001:3000"  # Use 3001 instead of 3000
```

**Out of disk space:**
```bash
# Clean up Docker volumes
docker volume prune

# Remove all stopped containers
docker container prune
```

---

## Full Integration Test

Run all tests end-to-end:

```bash
#!/bin/bash

echo "🚀 Starting full integration test..."

# 1. MongoDB CRUD
echo "✅ Testing MongoDB CRUD..."
USER_ID=$(curl -s -X POST http://localhost:3000/users \
  -H "Content-Type: application/json" \
  -d '{"name":"Test","email":"test@example.com","age":25}' | jq -r '._id')

curl -s http://localhost:3000/users/$USER_ID | jq .
curl -s -X DELETE http://localhost:3000/users/$USER_ID
echo "✅ MongoDB CRUD works!\n"

# 2. Kafka Producer/Consumer
echo "✅ Testing Kafka..."
curl -s -X POST http://localhost:3001/events/user \
  -H "Content-Type: application/json" \
  -d '{"userId":"123","action":"created","data":{}}' | jq .
echo "✅ Kafka works!\n"

# 3. RabbitMQ Producer/Consumer
echo "✅ Testing RabbitMQ..."
curl -s -X POST http://localhost:3002/queue/email \
  -H "Content-Type: application/json" \
  -d '{"to":"test@example.com","subject":"Test"}' | jq .
echo "✅ RabbitMQ works!\n"

echo "🎉 All services working!"
```

---

## Next Steps

- Deploy to Kubernetes using Helm charts
- Set up CI/CD pipeline with Docker images
- Configure monitoring with Prometheus + Grafana
- Implement automated backups
- Enable high availability with replication

