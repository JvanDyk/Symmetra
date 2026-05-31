# 🐳 Symmetra Docker Stack

Complete containerized environment for Symmetra with all services integrated and ready to use.

## What's Included

✅ **PostgreSQL 16** — SQL database  
✅ **MongoDB 7** — Document database  
✅ **Kafka 7.6** — Event streaming  
✅ **Zookeeper 7.6** — Kafka coordination  
✅ **RabbitMQ 3.13** — Message queue  
✅ **Kafka UI** — Monitoring dashboard (port 8080)  
✅ **RabbitMQ Management** — Queue management (port 15672)  

✅ **MongoDB CRUD API** (port 3000) — Complete CRUD operations  
✅ **Kafka Producer** (port 3001) — Event publishing API  
✅ **Kafka Consumer** — Background event processor  
✅ **RabbitMQ Producer** (port 3002) — Queue publishing API  
✅ **RabbitMQ Consumer** — Background message processor  

---

## Installation

### Prerequisites
- Docker & Docker Compose installed
- ~8GB free disk space
- Ports 3000-3002, 5432, 27017, 5672, 9092, 2181 available

### Setup (Choose One)

**Option 1: Quick Start (Linux/Mac)**
```bash
chmod +x start-services.sh
./start-services.sh
```

**Option 2: Quick Start (Windows)**
```bash
start-services.bat
```

**Option 3: Manual**
```bash
cp .env.example .env
docker-compose up -d
```

---

## First Steps

### 1. Verify Everything is Running
```bash
docker-compose ps
```

Expected output:
```
NAME                              STATUS
symmetra-postgres                 Up (healthy)
symmetra-mongodb                  Up (healthy)
symmetra-zookeeper                Up
symmetra-kafka                    Up (healthy)
symmetra-kafka-ui                 Up
symmetra-rabbitmq                 Up (healthy)
symmetra-mongo-crud               Up
symmetra-kafka-producer           Up
symmetra-kafka-consumer           Up
symmetra-rabbitmq-producer        Up
symmetra-rabbitmq-consumer        Up
```

### 2. Test MongoDB CRUD API
```bash
# Create a user
curl -X POST http://localhost:3000/users \
  -H "Content-Type: application/json" \
  -d '{
    "name": "Alice",
    "email": "alice@example.com",
    "age": 30
  }'
```

You should get back:
```json
{
  "_id": "507f1f77bcf86cd799439011",
  "name": "Alice",
  "email": "alice@example.com",
  "age": 30,
  "createdAt": "2026-05-31T12:00:00.000Z"
}
```

### 3. View Kafka Dashboard
```
http://localhost:8080
```

### 4. View RabbitMQ Dashboard
```
http://localhost:15672
Username: guest
Password: guest
```

---

## Common Commands

### View Logs
```bash
# All services
docker-compose logs -f

# Specific service
docker-compose logs -f symmify-mongo-crud
docker-compose logs -f symmify-kafka-producer
docker-compose logs -f symmify-rabbitmq-producer
```

### Stop Services
```bash
# Stop but keep data
docker-compose stop

# Stop and remove containers
docker-compose down

# Stop and remove everything (including volumes)
docker-compose down -v
```

### Restart Services
```bash
# Restart everything
docker-compose restart

# Restart specific service
docker-compose restart mongodb
docker-compose restart kafka
```

### Check Service Health
```bash
docker-compose ps
```

---

## Environment Variables

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
```

Then restart services:
```bash
docker-compose down
docker-compose up -d
```

---

## API Endpoints Reference

### MongoDB CRUD (http://localhost:3000)

| Method | Endpoint | Purpose |
|--------|----------|---------|
| POST | `/users` | Create user |
| GET | `/users` | List all users |
| GET | `/users/:id` | Get single user |
| PUT | `/users/:id` | Update user |
| DELETE | `/users/:id` | Delete user |
| GET | `/health` | Health check |

**Example: Create user**
```bash
curl -X POST http://localhost:3000/users \
  -H "Content-Type: application/json" \
  -d '{
    "name": "Bob",
    "email": "bob@example.com",
    "age": 25
  }'
```

**Example: Update user**
```bash
curl -X PUT http://localhost:3000/users/{user_id} \
  -H "Content-Type: application/json" \
  -d '{"age": 26}'
```

### Kafka Producer (http://localhost:3001)

| Method | Endpoint | Purpose |
|--------|----------|---------|
| POST | `/events/user` | Publish user event |
| POST | `/events/order` | Publish order event |
| POST | `/events/batch` | Batch publish events |

**Example: Publish user event**
```bash
curl -X POST http://localhost:3001/events/user \
  -H "Content-Type: application/json" \
  -d '{
    "userId": "550e8400-e29b-41d4-a716-446655440000",
    "action": "created",
    "data": {"name": "Charlie", "email": "charlie@example.com"}
  }'
```

### RabbitMQ Producer (http://localhost:3002)

| Method | Endpoint | Purpose |
|--------|----------|---------|
| POST | `/queue/email` | Queue email |
| POST | `/queue/notification` | Queue notification |
| POST | `/queue/task` | Queue task |

**Example: Queue email**
```bash
curl -X POST http://localhost:3002/queue/email \
  -H "Content-Type: application/json" \
  -d '{
    "to": "user@example.com",
    "subject": "Hello!",
    "template": "welcome",
    "data": {"name": "David"}
  }'
```

---

## Monitoring & Debugging

### View Database Contents

**MongoDB:**
```bash
docker-compose exec mongodb mongosh --eval "
  const db = connect('mongodb://admin:mongo123@localhost:27017/symmetra?authSource=admin');
  db.users.find().pretty()
"
```

**PostgreSQL:**
```bash
docker-compose exec postgres psql -U postgres -d symmetra -c "SELECT * FROM users LIMIT 10;"
```

### View Kafka Topics

```bash
docker-compose exec kafka kafka-topics --list --bootstrap-server localhost:9092
```

View topic messages:
```bash
docker-compose exec kafka kafka-console-consumer \
  --bootstrap-server localhost:9092 \
  --topic user-events \
  --from-beginning \
  --max-messages 10
```

### View RabbitMQ Queues

Via UI: http://localhost:15672 → Queues tab

Or CLI:
```bash
docker-compose exec rabbitmq rabbitmqctl list_queues
```

### Check Application Logs

**MongoDB API:**
```bash
docker-compose logs symmify-mongo-crud -f
```

**Kafka Consumer:**
```bash
docker-compose logs symmify-kafka-consumer -f
```

**RabbitMQ Consumer:**
```bash
docker-compose logs symmify-rabbitmq-consumer -f
```

---

## Troubleshooting

### Services won't start
```bash
# Check Docker is running
docker ps

# Review logs
docker-compose logs

# Try rebuilding
docker-compose up -d --build
```

### Port already in use
Change ports in `docker-compose.yml`:
```yaml
services:
  symmify-mongo-crud:
    ports:
      - "3000:3000"  # Change first number to free port
```

### Out of disk space
```bash
# Remove unused volumes
docker volume prune

# Remove unused images
docker image prune

# Clean everything
docker system prune -a
```

### Database connection issues
```bash
# Verify credentials in .env
cat .env

# Check service is healthy
docker-compose ps

# Restart the database service
docker-compose restart mongodb
docker-compose restart postgres
```

### Kafka not responding
```bash
# Check Zookeeper is running
docker-compose logs zookeeper

# Restart both
docker-compose restart zookeeper kafka
```

### RabbitMQ management UI not accessible
```bash
# Check RabbitMQ is running
docker-compose logs rabbitmq

# Try different port (sometimes 15673)
http://localhost:15672
```

---

## Performance Tuning

### Increase resource limits
Edit `docker-compose.yml`:
```yaml
services:
  mongodb:
    deploy:
      resources:
        limits:
          cpus: '2'
          memory: 2G
```

### Database optimization
```bash
# MongoDB index optimization
docker-compose exec mongodb mongosh --eval "
  db.users.createIndex({ email: 1 })
"

# PostgreSQL VACUUM
docker-compose exec postgres vacuumdb -U postgres symmetra
```

---

## Backup & Restore

### MongoDB Backup
```bash
docker-compose exec mongodb mongodump \
  --out /backup \
  -u admin \
  -p mongo123 \
  --authenticationDatabase admin
```

### PostgreSQL Backup
```bash
docker-compose exec postgres pg_dump \
  -U postgres \
  symmetra > backup.sql
```

### Restore from Backup
```bash
# MongoDB
docker-compose exec mongodb mongorestore \
  /backup \
  -u admin \
  -p mongo123 \
  --authenticationDatabase admin

# PostgreSQL
docker-compose exec postgres psql -U postgres symmetra < backup.sql
```

---

## Cleanup

### Remove All Services
```bash
# Keep volumes
docker-compose down

# Remove everything
docker-compose down -v
```

### Remove Specific Service
```bash
docker-compose rm -s symmify-mongo-crud
```

---

## Next Steps

1. **Explore APIs** — Test all endpoints in [API Reference](#api-endpoints-reference)
2. **View Dashboards** — Monitor Kafka (8080) and RabbitMQ (15672)
3. **Check Logs** — Review service logs for errors
4. **Read Documentation** — See [DOCKER_SETUP.md](DOCKER_SETUP.md) for detailed guide
5. **Deploy** — Container-ready for Kubernetes, Docker Swarm, or cloud platforms

---

## Support

For issues or questions:
- Check logs: `docker-compose logs -f`
- Review [DOCKER_SETUP.md](DOCKER_SETUP.md)
- Read [TESTING_GUIDE.md](TESTING_GUIDE.md)
- Check service health: `docker-compose ps`

