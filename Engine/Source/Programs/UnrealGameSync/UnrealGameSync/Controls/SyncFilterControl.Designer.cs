using System.Windows.Forms;

namespace UnrealGameSync.Controls
{
	partial class SyncFilterControl
	{
		/// <summary> 
		/// Required designer variable.
		/// </summary>
		private System.ComponentModel.IContainer components = null;

		/// <summary> 
		/// Clean up any resources being used.
		/// </summary>
		/// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
		protected override void Dispose(bool disposing)
		{
			if (disposing && (components != null))
			{
				components.Dispose();
			}
			base.Dispose(disposing);
		}

		#region Component Designer generated code

		/// <summary> 
		/// Required method for Designer support - do not modify 
		/// the contents of this method with the code editor.
		/// </summary>
		private void InitializeComponent()
		{
			components = new System.ComponentModel.Container();
			ViewGroupBox = new GroupBox();
			CustomViewTableLayoutPanel = new TableLayoutPanel();
			ViewDataGrid = new DataGridView();
			flowLayoutPanel1 = new FlowLayoutPanel();
			CopyCustomViewsToClipboard = new Button();
			PasteCustomViewsToClipboard = new Button();
			ClearCustomViewsToClipboard = new Button();
			SyntaxButton = new LinkLabel();
			CategoriesGroupBox = new GroupBox();
			tableLayoutPanel4 = new TableLayoutPanel();
			CategoriesCheckList = new CheckedListBox();
			SplitContainer = new SplitContainer();
			tableLayoutPanel1 = new TableLayoutPanel();
			groupBox1 = new GroupBox();
			tableLayoutPanel3 = new TableLayoutPanel();
			SyncLocalProjects = new CheckBox();
			SyncAllProjects = new CheckBox();
			IncludeAllProjectsInSolution = new CheckBox();
			GenerateUprojectSpecificSolution = new CheckBox();
			CategoriesToolTip = new ToolTip(components);
			ViewGroupBox.SuspendLayout();
			CustomViewTableLayoutPanel.SuspendLayout();
			((System.ComponentModel.ISupportInitialize)ViewDataGrid).BeginInit();
			flowLayoutPanel1.SuspendLayout();
			CategoriesGroupBox.SuspendLayout();
			tableLayoutPanel4.SuspendLayout();
			((System.ComponentModel.ISupportInitialize)SplitContainer).BeginInit();
			SplitContainer.Panel1.SuspendLayout();
			SplitContainer.Panel2.SuspendLayout();
			SplitContainer.SuspendLayout();
			tableLayoutPanel1.SuspendLayout();
			groupBox1.SuspendLayout();
			tableLayoutPanel3.SuspendLayout();
			SuspendLayout();
			// 
			// ViewGroupBox
			// 
			ViewGroupBox.Controls.Add(CustomViewTableLayoutPanel);
			ViewGroupBox.Controls.Add(SyntaxButton);
			ViewGroupBox.Dock = DockStyle.Fill;
			ViewGroupBox.Location = new System.Drawing.Point(0, 0);
			ViewGroupBox.Name = "ViewGroupBox";
			ViewGroupBox.Padding = new Padding(16, 8, 16, 8);
			ViewGroupBox.Size = new System.Drawing.Size(1008, 301);
			ViewGroupBox.TabIndex = 5;
			ViewGroupBox.TabStop = false;
			ViewGroupBox.Text = "Custom View";
			// 
			// CustomViewTableLayoutPanel
			// 
			CustomViewTableLayoutPanel.ColumnCount = 1;
			CustomViewTableLayoutPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
			CustomViewTableLayoutPanel.Controls.Add(ViewDataGrid, 0, 1);
			CustomViewTableLayoutPanel.Controls.Add(flowLayoutPanel1, 0, 0);
			CustomViewTableLayoutPanel.Dock = DockStyle.Fill;
			CustomViewTableLayoutPanel.Location = new System.Drawing.Point(16, 27);
			CustomViewTableLayoutPanel.Name = "CustomViewTableLayoutPanel";
			CustomViewTableLayoutPanel.RowCount = 2;
			CustomViewTableLayoutPanel.RowStyles.Add(new RowStyle(SizeType.Absolute, 45F));
			CustomViewTableLayoutPanel.RowStyles.Add(new RowStyle());
			CustomViewTableLayoutPanel.Size = new System.Drawing.Size(976, 266);
			CustomViewTableLayoutPanel.TabIndex = 9;
			// 
			// ViewDataGrid
			// 
			ViewDataGrid.AllowUserToResizeColumns = false;
			ViewDataGrid.BackgroundColor = System.Drawing.SystemColors.Window;
			ViewDataGrid.BorderStyle = BorderStyle.None;
			ViewDataGrid.CellBorderStyle = DataGridViewCellBorderStyle.None;
			ViewDataGrid.ColumnHeadersHeight = 29;
			ViewDataGrid.Dock = DockStyle.Fill;
			ViewDataGrid.Font = new System.Drawing.Font("Courier New", 8F);
			ViewDataGrid.ImeMode = ImeMode.Off;
			ViewDataGrid.Location = new System.Drawing.Point(0, 45);
			ViewDataGrid.Margin = new Padding(0);
			ViewDataGrid.MultiSelect = false;
			ViewDataGrid.Name = "ViewDataGrid";
			ViewDataGrid.RowHeadersWidth = 51;
			ViewDataGrid.RowHeadersWidthSizeMode = DataGridViewRowHeadersWidthSizeMode.DisableResizing;
			ViewDataGrid.RowTemplate.Height = 18;
			ViewDataGrid.RowTemplate.Resizable = DataGridViewTriState.False;
			ViewDataGrid.ScrollBars = ScrollBars.Vertical;
			ViewDataGrid.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
			ViewDataGrid.Size = new System.Drawing.Size(976, 234);
			ViewDataGrid.TabIndex = 6;
			// 
			// flowLayoutPanel1
			// 
			flowLayoutPanel1.Controls.Add(CopyCustomViewsToClipboard);
			flowLayoutPanel1.Controls.Add(PasteCustomViewsToClipboard);
			flowLayoutPanel1.Controls.Add(ClearCustomViewsToClipboard);
			flowLayoutPanel1.Dock = DockStyle.Fill;
			flowLayoutPanel1.Location = new System.Drawing.Point(3, 3);
			flowLayoutPanel1.Name = "flowLayoutPanel1";
			flowLayoutPanel1.Size = new System.Drawing.Size(970, 39);
			flowLayoutPanel1.TabIndex = 7;
			// 
			// CopyCustomViewsToClipboard
			// 
			CopyCustomViewsToClipboard.AutoSize = true;
			CopyCustomViewsToClipboard.Location = new System.Drawing.Point(3, 3);
			CopyCustomViewsToClipboard.Name = "CopyCustomViewsToClipboard";
			CopyCustomViewsToClipboard.Size = new System.Drawing.Size(75, 29);
			CopyCustomViewsToClipboard.TabIndex = 0;
			CopyCustomViewsToClipboard.Text = "copy";
			CopyCustomViewsToClipboard.UseVisualStyleBackColor = true;
			CopyCustomViewsToClipboard.Click += CopyCustomViewsToClipboard_Click;
			// 
			// PasteCustomViewsToClipboard
			// 
			PasteCustomViewsToClipboard.AutoSize = true;
			PasteCustomViewsToClipboard.Location = new System.Drawing.Point(84, 3);
			PasteCustomViewsToClipboard.Name = "PasteCustomViewsToClipboard";
			PasteCustomViewsToClipboard.Size = new System.Drawing.Size(75, 29);
			PasteCustomViewsToClipboard.TabIndex = 1;
			PasteCustomViewsToClipboard.Text = "paste";
			PasteCustomViewsToClipboard.UseVisualStyleBackColor = true;
			PasteCustomViewsToClipboard.Click += PasteCustomViewsToClipboard_Click;
			// 
			// ClearCustomViewsToClipboard
			// 
			ClearCustomViewsToClipboard.AutoSize = true;
			ClearCustomViewsToClipboard.Location = new System.Drawing.Point(165, 3);
			ClearCustomViewsToClipboard.Name = "ClearCustomViewsToClipboard";
			ClearCustomViewsToClipboard.Size = new System.Drawing.Size(75, 29);
			ClearCustomViewsToClipboard.TabIndex = 2;
			ClearCustomViewsToClipboard.Text = "clear";
			ClearCustomViewsToClipboard.UseVisualStyleBackColor = true;
			ClearCustomViewsToClipboard.Click += ClearCustomViewsToClipboard_Click;
			// 
			// SyntaxButton
			// 
			SyntaxButton.Anchor = AnchorStyles.Top | AnchorStyles.Right;
			SyntaxButton.AutoSize = true;
			SyntaxButton.Location = new System.Drawing.Point(933, 0);
			SyntaxButton.Name = "SyntaxButton";
			SyntaxButton.Size = new System.Drawing.Size(49, 19);
			SyntaxButton.TabIndex = 7;
			SyntaxButton.TabStop = true;
			SyntaxButton.Text = "Syntax";
			SyntaxButton.LinkClicked += SyntaxButton_LinkClicked;
			// 
			// CategoriesGroupBox
			// 
			CategoriesGroupBox.AutoSizeMode = AutoSizeMode.GrowAndShrink;
			CategoriesGroupBox.Controls.Add(tableLayoutPanel4);
			CategoriesGroupBox.Dock = DockStyle.Fill;
			CategoriesGroupBox.Location = new System.Drawing.Point(0, 102);
			CategoriesGroupBox.Margin = new Padding(0, 3, 0, 0);
			CategoriesGroupBox.Name = "CategoriesGroupBox";
			CategoriesGroupBox.Padding = new Padding(16, 8, 16, 8);
			CategoriesGroupBox.Size = new System.Drawing.Size(1008, 304);
			CategoriesGroupBox.TabIndex = 4;
			CategoriesGroupBox.TabStop = false;
			CategoriesGroupBox.Text = "Categories";
			// 
			// tableLayoutPanel4
			// 
			tableLayoutPanel4.ColumnCount = 1;
			tableLayoutPanel4.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50F));
			tableLayoutPanel4.Controls.Add(CategoriesCheckList, 0, 0);
			tableLayoutPanel4.Dock = DockStyle.Fill;
			tableLayoutPanel4.Location = new System.Drawing.Point(16, 27);
			tableLayoutPanel4.Name = "tableLayoutPanel4";
			tableLayoutPanel4.RowCount = 1;
			tableLayoutPanel4.RowStyles.Add(new RowStyle(SizeType.Percent, 50F));
			tableLayoutPanel4.Size = new System.Drawing.Size(976, 269);
			tableLayoutPanel4.TabIndex = 8;
			// 
			// CategoriesCheckList
			// 
			CategoriesCheckList.BorderStyle = BorderStyle.None;
			CategoriesCheckList.CheckOnClick = true;
			CategoriesCheckList.Dock = DockStyle.Fill;
			CategoriesCheckList.FormattingEnabled = true;
			CategoriesCheckList.IntegralHeight = false;
			CategoriesCheckList.Location = new System.Drawing.Point(3, 3);
			CategoriesCheckList.Name = "CategoriesCheckList";
			CategoriesCheckList.Size = new System.Drawing.Size(970, 263);
			CategoriesCheckList.Sorted = true;
			CategoriesCheckList.TabIndex = 7;
			// 
			// SplitContainer
			// 
			SplitContainer.Dock = DockStyle.Fill;
			SplitContainer.Location = new System.Drawing.Point(7, 7);
			SplitContainer.Name = "SplitContainer";
			SplitContainer.Orientation = Orientation.Horizontal;
			// 
			// SplitContainer.Panel1
			// 
			SplitContainer.Panel1.Controls.Add(tableLayoutPanel1);
			// 
			// SplitContainer.Panel2
			// 
			SplitContainer.Panel2.Controls.Add(ViewGroupBox);
			SplitContainer.Size = new System.Drawing.Size(1008, 719);
			SplitContainer.SplitterDistance = 406;
			SplitContainer.SplitterWidth = 12;
			SplitContainer.TabIndex = 8;
			// 
			// tableLayoutPanel1
			// 
			tableLayoutPanel1.AutoSize = true;
			tableLayoutPanel1.ColumnCount = 1;
			tableLayoutPanel1.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
			tableLayoutPanel1.Controls.Add(groupBox1, 0, 0);
			tableLayoutPanel1.Controls.Add(CategoriesGroupBox, 0, 1);
			tableLayoutPanel1.Dock = DockStyle.Fill;
			tableLayoutPanel1.Location = new System.Drawing.Point(0, 0);
			tableLayoutPanel1.Margin = new Padding(0);
			tableLayoutPanel1.Name = "tableLayoutPanel1";
			tableLayoutPanel1.RowCount = 2;
			tableLayoutPanel1.RowStyles.Add(new RowStyle());
			tableLayoutPanel1.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
			tableLayoutPanel1.Size = new System.Drawing.Size(1008, 406);
			tableLayoutPanel1.TabIndex = 8;
			// 
			// groupBox1
			// 
			groupBox1.AutoSize = true;
			groupBox1.AutoSizeMode = AutoSizeMode.GrowAndShrink;
			groupBox1.Controls.Add(tableLayoutPanel3);
			groupBox1.Dock = DockStyle.Fill;
			groupBox1.Location = new System.Drawing.Point(0, 3);
			groupBox1.Margin = new Padding(0, 3, 0, 3);
			groupBox1.Name = "groupBox1";
			groupBox1.Padding = new Padding(16, 8, 16, 8);
			groupBox1.Size = new System.Drawing.Size(1008, 93);
			groupBox1.TabIndex = 8;
			groupBox1.TabStop = false;
			groupBox1.Text = "General";
			// 
			// tableLayoutPanel3
			// 
			tableLayoutPanel3.AutoSize = true;
			tableLayoutPanel3.AutoSizeMode = AutoSizeMode.GrowAndShrink;
			tableLayoutPanel3.ColumnCount = 2;
			tableLayoutPanel3.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50F));
			tableLayoutPanel3.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50F));
			tableLayoutPanel3.Controls.Add(SyncLocalProjects, 1, 0);
			tableLayoutPanel3.Controls.Add(SyncAllProjects, 0, 0);
			tableLayoutPanel3.Controls.Add(IncludeAllProjectsInSolution, 0, 1);
			tableLayoutPanel3.Controls.Add(GenerateUprojectSpecificSolution, 1, 1);
			tableLayoutPanel3.Dock = DockStyle.Fill;
			tableLayoutPanel3.Location = new System.Drawing.Point(16, 27);
			tableLayoutPanel3.Name = "tableLayoutPanel3";
			tableLayoutPanel3.RowCount = 2;
			tableLayoutPanel3.RowStyles.Add(new RowStyle(SizeType.Percent, 50F));
			tableLayoutPanel3.RowStyles.Add(new RowStyle(SizeType.Percent, 50F));
			tableLayoutPanel3.Size = new System.Drawing.Size(976, 58);
			tableLayoutPanel3.TabIndex = 8;
			// 
			// SyncLocalProjects
			// 
			SyncLocalProjects.Anchor = AnchorStyles.Left;
			SyncLocalProjects.AutoSize = true;
			SyncLocalProjects.Location = new System.Drawing.Point(491, 3);
			SyncLocalProjects.Name = "SyncLocalProjects";
			SyncLocalProjects.Size = new System.Drawing.Size(142, 23);
			SyncLocalProjects.TabIndex = 9;
			SyncLocalProjects.Text = "Sync local projects";
			SyncLocalProjects.UseVisualStyleBackColor = true;
			// 
			// SyncAllProjects
			// 
			SyncAllProjects.Anchor = AnchorStyles.Left;
			SyncAllProjects.AutoSize = true;
			SyncAllProjects.Location = new System.Drawing.Point(3, 3);
			SyncAllProjects.Name = "SyncAllProjects";
			SyncAllProjects.Size = new System.Drawing.Size(189, 23);
			SyncAllProjects.TabIndex = 6;
			SyncAllProjects.Text = "Sync all projects in stream";
			SyncAllProjects.UseVisualStyleBackColor = true;
			// 
			// IncludeAllProjectsInSolution
			// 
			IncludeAllProjectsInSolution.Anchor = AnchorStyles.Left;
			IncludeAllProjectsInSolution.AutoSize = true;
			IncludeAllProjectsInSolution.Location = new System.Drawing.Point(3, 32);
			IncludeAllProjectsInSolution.Name = "IncludeAllProjectsInSolution";
			IncludeAllProjectsInSolution.Size = new System.Drawing.Size(258, 23);
			IncludeAllProjectsInSolution.TabIndex = 7;
			IncludeAllProjectsInSolution.Text = "Include all synced projects in solution";
			IncludeAllProjectsInSolution.UseVisualStyleBackColor = true;
			// 
			// GenerateUprojectSpecificSolution
			// 
			GenerateUprojectSpecificSolution.Anchor = AnchorStyles.Left;
			GenerateUprojectSpecificSolution.AutoSize = true;
			GenerateUprojectSpecificSolution.Location = new System.Drawing.Point(491, 32);
			GenerateUprojectSpecificSolution.Name = "GenerateUprojectSpecificSolution";
			GenerateUprojectSpecificSolution.Size = new System.Drawing.Size(296, 23);
			GenerateUprojectSpecificSolution.TabIndex = 8;
			GenerateUprojectSpecificSolution.Text = "Generate minimal .uproject specific solution";
			GenerateUprojectSpecificSolution.UseVisualStyleBackColor = true;
			// 
			// SyncFilterControl
			// 
			AutoScaleDimensions = new System.Drawing.SizeF(120F, 120F);
			AutoScaleMode = AutoScaleMode.Dpi;
			BackColor = System.Drawing.SystemColors.Window;
			Controls.Add(SplitContainer);
			Font = new System.Drawing.Font("Segoe UI", 8.25F);
			Name = "SyncFilterControl";
			Padding = new Padding(7);
			Size = new System.Drawing.Size(1022, 733);
			ViewGroupBox.ResumeLayout(false);
			ViewGroupBox.PerformLayout();
			CustomViewTableLayoutPanel.ResumeLayout(false);
			((System.ComponentModel.ISupportInitialize)ViewDataGrid).EndInit();
			flowLayoutPanel1.ResumeLayout(false);
			flowLayoutPanel1.PerformLayout();
			CategoriesGroupBox.ResumeLayout(false);
			tableLayoutPanel4.ResumeLayout(false);
			SplitContainer.Panel1.ResumeLayout(false);
			SplitContainer.Panel1.PerformLayout();
			SplitContainer.Panel2.ResumeLayout(false);
			((System.ComponentModel.ISupportInitialize)SplitContainer).EndInit();
			SplitContainer.ResumeLayout(false);
			tableLayoutPanel1.ResumeLayout(false);
			tableLayoutPanel1.PerformLayout();
			groupBox1.ResumeLayout(false);
			groupBox1.PerformLayout();
			tableLayoutPanel3.ResumeLayout(false);
			tableLayoutPanel3.PerformLayout();
			ResumeLayout(false);
		}

		#endregion

		private System.Windows.Forms.GroupBox ViewGroupBox;
		private System.Windows.Forms.GroupBox CategoriesGroupBox;
		public System.Windows.Forms.CheckedListBox CategoriesCheckList;
		private System.Windows.Forms.SplitContainer SplitContainer;
		private System.Windows.Forms.LinkLabel SyntaxButton;
		private System.Windows.Forms.DataGridView ViewDataGrid;
		private System.Windows.Forms.TableLayoutPanel tableLayoutPanel1;
		private System.Windows.Forms.GroupBox groupBox1;
		public System.Windows.Forms.CheckBox SyncAllProjects;
		public System.Windows.Forms.CheckBox IncludeAllProjectsInSolution;
		public System.Windows.Forms.CheckBox GenerateUprojectSpecificSolution;
		private System.Windows.Forms.TableLayoutPanel tableLayoutPanel3;
		private System.Windows.Forms.TableLayoutPanel tableLayoutPanel4;
		public CheckBox SyncLocalProjects;
		private ToolTip CategoriesToolTip;
		private TableLayoutPanel CustomViewTableLayoutPanel;
		private FlowLayoutPanel flowLayoutPanel1;
		private Button CopyCustomViewsToClipboard;
		private Button PasteCustomViewsToClipboard;
		private Button ClearCustomViewsToClipboard;
	}
}
