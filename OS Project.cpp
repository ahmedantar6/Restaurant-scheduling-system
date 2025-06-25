#include <iostream>
#include <queue>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <map>
#include <set>

using namespace std;

// Structure to represent an order
struct Order {
    int orderID;               // Unique ID for the order
    vector<string> foods;      // List of food items in the order
    int table;                 // Table number assigned to the order
    bool isCompleted;          // Whether the order is completed
    int workerID;              // ID of the worker processing the order
};

// Structure to represent worker credentials
struct WorkerCredential {
    int workerId;              // Unique ID for the worker
    string fullName;           // Full name of the worker
    string password;           // Password for the worker
    int defaultTask;           // Default task assigned to the worker
};

// List of task names corresponding to worker tasks
const vector<string> taskNames = { "Cook", "Serve", "Clean Table", "Wash Dishes", "Select Table" };

// Shared resources
queue<Order> orderQueue;       // Queue to hold orders
mutex queueMutex;              // Mutex to protect access to the order queue
condition_variable cv;         // Condition variable to notify workers of new orders
mutex coutMutex;               // Mutex to protect console output
vector<bool> tables(5, true);  // Vector to track table availability (true = available)
vector<Order> completedOrders; // List of completed orders
vector<string> waitingList;    // List of guests in the waiting list
int orderCounter = 1;          // Counter to generate unique order IDs
bool shutdownFlag = false;     // Flag to signal shutdown to worker threads

vector<WorkerCredential> workerCredentials; // List of registered workers
set<int> usedWorkerIds;                     // Set of used worker IDs to ensure uniqueness

// Function to display the status of tables
void displayAvailableTables() {
    cout << "\nTable Status:\n";
    for (size_t i = 0; i < tables.size(); ++i) {
        cout << "Table " << i + 1 << ": " << (tables[i] ? "Available" : "Unavailable") << endl;
    }
    cout << endl;
}

// Function to display the food menu
void displayFoodMenu(const vector<string>& foodList) {
    cout << "Available Food Items:\n";
    for (size_t i = 0; i < foodList.size(); ++i) {
        cout << i + 1 << ". " << foodList[i] << endl;
    }
}

// Function to display the waiting list
void displayWaitingList() {
    cout << "\nCurrent Waiting List (" << waitingList.size() << "/10):\n";
    for (const auto& guest : waitingList) {
        cout << "- " << guest << endl;
    }
    cout << "-----------------------------\n";
}

// Function executed by each worker thread
void workerFunction(int workerId) {
    WorkerCredential currentWorker;
    {
        // Find the worker's credentials based on their ID
        auto it = find_if(workerCredentials.begin(), workerCredentials.end(),
            [workerId](const WorkerCredential& wc) { return wc.workerId == workerId; });
        if (it != workerCredentials.end()) {
            currentWorker = *it;
        }
    }

    while (true) {
        // Lock the queue and wait for new orders or shutdown signal
        unique_lock<mutex> lock(queueMutex);
        cv.wait(lock, [] { return !orderQueue.empty() || shutdownFlag; });
        if (shutdownFlag && orderQueue.empty())
            break; // Exit if shutdown is signaled and no orders are left

        // Retrieve the next order from the queue
        Order currentOrder = orderQueue.front();
        orderQueue.pop();
        lock.unlock();

        string taskDescription;

        // Assign a table to the order if not already assigned
        if (currentOrder.table == 0 && currentWorker.defaultTask != 5) {
            lock_guard<mutex> tblLock(queueMutex);
            for (size_t i = 0; i < tables.size(); ++i) {
                if (tables[i]) {
                    currentOrder.table = i + 1;
                    tables[i] = false; // Mark the table as unavailable
                    break;
                }
            }
            if (currentOrder.table == 0) {
                // If no table is available, requeue the order and continue
                lock_guard<mutex> lock(queueMutex);
                orderQueue.push(currentOrder);
                cv.notify_one();
                continue;
            }
        }

        // Output the worker's task to the console
        {
            lock_guard<mutex> coutLock(coutMutex);
            cout << "\nWorker " << currentWorker.workerId << " (" << currentWorker.fullName
                << ") is processing Order " << currentOrder.orderID;
            if (currentOrder.table != 0)
                cout << " (Assigned Table " << currentOrder.table << ")";
            cout << endl;
        }

        // Perform the worker's task for each food item in the order
        for (const auto& food : currentOrder.foods) {
            switch (currentWorker.defaultTask) {
            case 1:
                taskDescription = taskNames[0] + " " + food;
                cout << "Cooking " << food << "...\n";
                break;
            case 2:
                taskDescription = taskNames[1] + " " + food;
                cout << "Serving " << food << "...\n";
                break;
            case 3:
                taskDescription = taskNames[2] + " (at Table " + to_string(currentOrder.table) + ")";
                cout << "Cleaning Table " << currentOrder.table << "...\n";
                break;
            case 4:
                taskDescription = taskNames[3] + " (at Table " + to_string(currentOrder.table) + ")";
                cout << "Washing Dishes for Table " << currentOrder.table << "...\n";
                break;
            case 5: {
                // Allow the worker to manually select a table
                bool validTableSelected = false;
                while (!validTableSelected) {
                    cout << "\nWorker " << currentWorker.workerId
                        << " (" << currentWorker.fullName << ") - Choose a table for Order "
                        << currentOrder.orderID << ":\n";
                    displayAvailableTables();
                    cout << "Enter table number: ";
                    int chosenTable;
                    cin >> chosenTable;
                    if (chosenTable < 1 || chosenTable >(int)tables.size()) {
                        cout << "Invalid table number. Try again.\n";
                        continue;
                    }
                    lock_guard<mutex> lock(queueMutex);
                    if (tables[chosenTable - 1]) {
                        tables[chosenTable - 1] = false;
                        currentOrder.table = chosenTable;
                        validTableSelected = true;
                    }
                    else {
                        cout << "Table " << chosenTable << " is unavailable. Choose another.\n";
                    }
                }
                taskDescription = taskNames[4] + " (set to Table " + to_string(currentOrder.table) + ")";
                break;
            }
            default:
                taskDescription = "No valid task selected";
                cout << "Invalid task for worker " << currentWorker.workerId << "\n";
                break;
            }
            this_thread::sleep_for(chrono::seconds(1)); // Simulate task duration
        }

        // Mark the order as completed and release the table
        currentOrder.foods.clear();
        currentOrder.isCompleted = true;
        currentOrder.workerID = currentWorker.workerId;

        {
            lock_guard<mutex> lock(queueMutex);
            completedOrders.push_back(currentOrder);
        }

        cout << "\nOrder " << currentOrder.orderID << " completed by Worker "
            << currentWorker.workerId << "\n";
    }
}

int main() {
    char role;
    cout << "Are you a guest or worker? (g/w): ";
    cin >> role;

    if (role == 'w' || role == 'W') {
        // Worker registration process
        cout << "\n=== Worker Registration ===\n";
        set<int> chosenTasks;
        int registered = 0;
        while (registered < 5) {
            WorkerCredential wc;
            while (true) {
                cout << "\nEnter unique ID for Worker: ";
                cin >> wc.workerId;
                if (usedWorkerIds.count(wc.workerId)) {
                    cout << "Worker ID already used. Please enter a different one.\n";
                }
                else {
                    usedWorkerIds.insert(wc.workerId);
                    break;
                }
            }
            cin.ignore();
            cout << "Worker " << wc.workerId << " - Enter full name: ";
            getline(cin, wc.fullName);

            while (true) {
                cout << "Worker " << wc.workerId << " - Enter password: ";
                getline(cin, wc.password);
                bool uniquePassword = true;
                for (const auto& existing : workerCredentials) {
                    if (existing.password == wc.password) {
                        cout << "Password already used. Please try again.\n";
                        uniquePassword = false;
                        break;
                    }
                }
                if (uniquePassword)
                    break;
            }

            int taskChoice;
            while (true) {
                if (chosenTasks.size() == 5) {
                    cout << "All tasks have been chosen by other workers. Exiting program.\n";
                    exit(0);
                }
                cout << "\nSelect your default task:\n";
                for (int i = 0; i < 5; ++i) {
                    cout << i + 1 << ". " << taskNames[i];
                    if (chosenTasks.count(i + 1))
                        cout << " (Already Chosen)";
                    cout << "\n";
                }
                cout << "Choice: ";
                cin >> taskChoice;
                if (taskChoice < 1 || taskChoice > 5) {
                    cout << "Invalid task choice. Try again.\n";
                    continue;
                }
                if (chosenTasks.count(taskChoice) > 0) {
                    cout << "Task " << taskNames[taskChoice - 1]
                        << " is already chosen by another worker. Please select another task.\n";
                    continue;
                }
                wc.defaultTask = taskChoice;
                chosenTasks.insert(taskChoice);
                break;
            }
            workerCredentials.push_back(wc);
            registered++;
        }

        // Display task assignments
        cout << "\n=== Task Assignment ===\n";
        for (const auto& wc : workerCredentials) {
            cout << "Task: " << taskNames[wc.defaultTask - 1] << " - Worker ID: " << wc.workerId << " (" << wc.fullName << ")\n";
        }

        // Create worker threads
        vector<thread> workers;
        for (const auto& wc : workerCredentials) {
            workers.emplace_back(workerFunction, wc.workerId);
        }

        // Wait for some time before shutting down
        this_thread::sleep_for(chrono::seconds(20));
        shutdownFlag = true;
        cv.notify_all();
        for (auto& worker : workers)
            worker.join();

        cout << "\nAll orders processed.\n";
    }
    else if (role == 'g' || role == 'G') {
        // Guest order placement process
        while (true) {
            char continueChoice;
            cout << "\nDo you want to place an order? (y/n): ";
            cin >> continueChoice;
            if (continueChoice == 'n' || continueChoice == 'N') {
                cout << "Exiting guest system. Goodbye!\n";
                break;
            }

            if (waitingList.size() >= 10) {
                cout << "Waiting list full. Try again later.\n";
                break;
            }

            vector<string> foodList = { "Pizza", "Burger", "Pasta", "Salad" };
            displayFoodMenu(foodList);
            cout << "Enter food numbers (space-separated) or type 'exit' to quit: ";
            cin.ignore();
            string input;
            getline(cin, input);
            if (input == "exit" || input == "EXIT") {
                cout << "Exiting guest system. Goodbye!\n";
                break;
            }
            vector<string> selectedFoods;
            stringstream ss(input);
            int choice;
            while (ss >> choice) {
                if (choice >= 1 && choice <= (int)foodList.size()) {
                    selectedFoods.push_back(foodList[choice - 1]);
                }
            }

            displayAvailableTables();
            int tableChoice;
            cout << "Choose a table number (1-5): ";
            cin >> tableChoice;
            cin.ignore();

            string guestName;
            cout << "Enter your name: ";
            getline(cin, guestName);

            if (tableChoice >= 1 && tableChoice <= 5) {
                if (!tables[tableChoice - 1]) {
                    cout << "Table is unavailable. Adding you to waiting list.\n";
                    waitingList.push_back(guestName + " (Table " + to_string(tableChoice) + ")");
                    displayWaitingList();
                    continue;
                }
                tables[tableChoice - 1] = false;
            }
            else {
                cout << "Invalid table number.\n";
                continue;
            }

            // Create a new order and add it to the queue
            Order newOrder;
            newOrder.orderID = orderCounter++;
            newOrder.foods = selectedFoods;
            newOrder.table = tableChoice;
            newOrder.isCompleted = false;
            newOrder.workerID = 0;

            {
                lock_guard<mutex> lock(queueMutex);
                orderQueue.push(newOrder);
            }
            cv.notify_one();

            cout << "Order placed. Your order ID: " << newOrder.orderID << endl;
            displayWaitingList();

            // Check if all tables are unavailable and the waiting list is full
            bool allTablesUnavailable = all_of(tables.begin(), tables.end(), [](bool t) { return !t; });
            if (allTablesUnavailable && waitingList.size() >= 10) {
                cout << "All tables are now unavailable and waiting list is full. Exiting guest system.\n";
                break;
            }
        }
    }
    else {
        cout << "Invalid input. Exiting...\n";
    }
    return 0;
}