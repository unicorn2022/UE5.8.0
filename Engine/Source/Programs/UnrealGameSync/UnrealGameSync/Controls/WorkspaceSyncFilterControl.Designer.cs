using System.Windows.Forms;

namespace UnrealGameSync.Controls
{
	partial class WorkspaceSyncFilterControl
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
			if (disposing)
			{
				if (_syncBaseContentTooltipTimer.Enabled)
				{
					_syncBaseContentTooltipTimer.Stop();
				}
				_syncBaseContentTooltipTimer.Dispose();

				if (_categoriesCheckListTooltipTimer.Enabled)
				{
					_categoriesCheckListTooltipTimer.Stop();
				}
				_categoriesCheckListTooltipTimer.Dispose();
			}

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
			CustomViewsHeaderPanel = new FlowLayoutPanel();
			CopyCustomViewsToClipboard = new Button();
			PasteCustomViewsToClipboard = new Button();
			ClearCustomViewsToClipboard = new Button();
			SyntaxButton = new LinkLabel();
			CategoriesGroupBox = new GroupBox();
			CategoriesTableLayoutPanel = new TableLayoutPanel();
			CategoriesCheckList = new CheckedListBox();
			SyncBaseContentFlowLayout = new FlowLayoutPanel();
			SyncBaseContentCheckBox = new CheckBox();
			PresetNameLabel = new Label();
			PresetComboBox = new ComboBox();
			SplitContainer = new SplitContainer();
			tableLayoutPanel1 = new TableLayoutPanel();
			groupBox1 = new GroupBox();
			tableLayoutPanel3 = new TableLayoutPanel();
			SyncLocalProjects = new CheckBox();
			SyncAllProjects = new CheckBox();
			IncludeAllProjectsInSolution = new CheckBox();
			GenerateUprojectSpecificSolution = new CheckBox();
			flowLayoutPanel3 = new FlowLayoutPanel();
			CategoriesToolTip = new ToolTip(components);
			SyncBaseContentToolTip = new ToolTip(components);
			ViewGroupBox.SuspendLayout();
			CustomViewTableLayoutPanel.SuspendLayout();
			((System.ComponentModel.ISupportInitialize)ViewDataGrid).BeginInit();
			CustomViewsHeaderPanel.SuspendLayout();
			CategoriesGroupBox.SuspendLayout();
			CategoriesTableLayoutPanel.SuspendLayout();
			SyncBaseContentFlowLayout.SuspendLayout();
			((System.ComponentModel.ISupportInitialize)SplitContainer).BeginInit();
			SplitContainer.Panel1.SuspendLayout();
			SplitContainer.Panel2.SuspendLayout();
			SplitContainer.SuspendLayout();
			tableLayoutPanel1.SuspendLayout();
			groupBox1.SuspendLayout();
			tableLayoutPanel3.SuspendLayout();
			flowLayoutPanel3.SuspendLayout();
			SuspendLayout();
			// 
			// ViewGroupBox
			// 
			ViewGroupBox.Controls.Add(CustomViewTableLayoutPanel);
			ViewGroupBox.Controls.Add(SyntaxButton);
			ViewGroupBox.Dock = DockStyle.Fill;
			ViewGroupBox.Location = new System.Drawing.Point(0, 0);
			ViewGroupBox.Margin = new Padding(0, 3, 0, 0);
			ViewGroupBox.Name = "ViewGroupBox";
			ViewGroupBox.Padding = new Padding(16, 8, 16, 8);
			ViewGroupBox.Size = new System.Drawing.Size(1008, 312);
			ViewGroupBox.TabIndex = 5;
			ViewGroupBox.TabStop = false;
			ViewGroupBox.Text = "Custom View";
			// 
			// CustomViewTableLayoutPanel
			// 
			CustomViewTableLayoutPanel.ColumnCount = 1;
			CustomViewTableLayoutPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
			CustomViewTableLayoutPanel.Controls.Add(ViewDataGrid, 0, 1);
			CustomViewTableLayoutPanel.Controls.Add(CustomViewsHeaderPanel, 0, 0);
			CustomViewTableLayoutPanel.Dock = DockStyle.Fill;
			CustomViewTableLayoutPanel.Location = new System.Drawing.Point(16, 23);
			CustomViewTableLayoutPanel.Name = "CustomViewTableLayoutPanel";
			CustomViewTableLayoutPanel.RowCount = 2;
			CustomViewTableLayoutPanel.RowStyles.Add(new RowStyle(SizeType.Absolute, 45F));
			CustomViewTableLayoutPanel.RowStyles.Add(new RowStyle());
			CustomViewTableLayoutPanel.Size = new System.Drawing.Size(976, 281);
			CustomViewTableLayoutPanel.TabIndex = 8;
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
			ViewDataGrid.Location = new System.Drawing.Point(2, 47);
			ViewDataGrid.Margin = new Padding(2);
			ViewDataGrid.MultiSelect = false;
			ViewDataGrid.Name = "ViewDataGrid";
			ViewDataGrid.RowHeadersWidth = 51;
			ViewDataGrid.RowHeadersWidthSizeMode = DataGridViewRowHeadersWidthSizeMode.DisableResizing;
			ViewDataGrid.RowTemplate.Height = 18;
			ViewDataGrid.RowTemplate.Resizable = DataGridViewTriState.False;
			ViewDataGrid.ScrollBars = ScrollBars.Vertical;
			ViewDataGrid.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
			ViewDataGrid.Size = new System.Drawing.Size(972, 241);
			ViewDataGrid.TabIndex = 6;
			// 
			// CustomViewsHeaderPanel
			// 
			CustomViewsHeaderPanel.Controls.Add(CopyCustomViewsToClipboard);
			CustomViewsHeaderPanel.Controls.Add(PasteCustomViewsToClipboard);
			CustomViewsHeaderPanel.Controls.Add(ClearCustomViewsToClipboard);
			CustomViewsHeaderPanel.Dock = DockStyle.Fill;
			CustomViewsHeaderPanel.Location = new System.Drawing.Point(3, 3);
			CustomViewsHeaderPanel.Name = "CustomViewsHeaderPanel";
			CustomViewsHeaderPanel.Size = new System.Drawing.Size(970, 39);
			CustomViewsHeaderPanel.TabIndex = 7;
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
			CopyCustomViewsToClipboard.Click += CopyViewsToClipBoard_Click;
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
			SyntaxButton.Size = new System.Drawing.Size(40, 13);
			SyntaxButton.TabIndex = 7;
			SyntaxButton.TabStop = true;
			SyntaxButton.Text = "Syntax";
			SyntaxButton.LinkClicked += SyntaxButton_LinkClicked;
			// 
			// CategoriesGroupBox
			// 
			CategoriesGroupBox.AutoSizeMode = AutoSizeMode.GrowAndShrink;
			CategoriesGroupBox.Controls.Add(CategoriesTableLayoutPanel);
			CategoriesGroupBox.Dock = DockStyle.Fill;
			CategoriesGroupBox.Location = new System.Drawing.Point(0, 145);
			CategoriesGroupBox.Margin = new Padding(0, 3, 0, 0);
			CategoriesGroupBox.Name = "CategoriesGroupBox";
			CategoriesGroupBox.Padding = new Padding(16, 8, 16, 8);
			CategoriesGroupBox.Size = new System.Drawing.Size(1008, 317);
			CategoriesGroupBox.TabIndex = 4;
			CategoriesGroupBox.TabStop = false;
			CategoriesGroupBox.Text = "Categories";
			// 
			// CategoriesTableLayoutPanel
			// 
			CategoriesTableLayoutPanel.ColumnCount = 1;
			CategoriesTableLayoutPanel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
			CategoriesTableLayoutPanel.Controls.Add(CategoriesCheckList, 0, 1);
			CategoriesTableLayoutPanel.Controls.Add(SyncBaseContentFlowLayout, 0, 0);
			CategoriesTableLayoutPanel.Dock = DockStyle.Fill;
			CategoriesTableLayoutPanel.Location = new System.Drawing.Point(16, 23);
			CategoriesTableLayoutPanel.Name = "CategoriesTableLayoutPanel";
			CategoriesTableLayoutPanel.RowCount = 2;
			CategoriesTableLayoutPanel.RowStyles.Add(new RowStyle());
			CategoriesTableLayoutPanel.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
			CategoriesTableLayoutPanel.Size = new System.Drawing.Size(976, 286);
			CategoriesTableLayoutPanel.TabIndex = 10;
			// 
			// CategoriesCheckList
			// 
			CategoriesCheckList.BorderStyle = BorderStyle.None;
			CategoriesCheckList.CheckOnClick = true;
			CategoriesCheckList.Dock = DockStyle.Fill;
			CategoriesCheckList.FormattingEnabled = true;
			CategoriesCheckList.IntegralHeight = false;
			CategoriesCheckList.Location = new System.Drawing.Point(3, 26);
			CategoriesCheckList.Name = "CategoriesCheckList";
			CategoriesCheckList.Size = new System.Drawing.Size(970, 257);
			CategoriesCheckList.Sorted = true;
			CategoriesCheckList.TabIndex = 1;
			// 
			// SyncBaseContentFlowLayout
			// 
			SyncBaseContentFlowLayout.AutoSize = true;
			SyncBaseContentFlowLayout.Controls.Add(SyncBaseContentCheckBox);
			SyncBaseContentFlowLayout.Dock = DockStyle.Fill;
			SyncBaseContentFlowLayout.Location = new System.Drawing.Point(0, 0);
			SyncBaseContentFlowLayout.Margin = new Padding(0);
			SyncBaseContentFlowLayout.Name = "SyncBaseContentFlowLayout";
			SyncBaseContentFlowLayout.Size = new System.Drawing.Size(976, 23);
			SyncBaseContentFlowLayout.TabIndex = 2;
			// 
			// SyncBaseContentCheckBox
			// 
			SyncBaseContentCheckBox.Anchor = AnchorStyles.Left | AnchorStyles.Right;
			SyncBaseContentCheckBox.AutoSize = true;
			SyncBaseContentCheckBox.Location = new System.Drawing.Point(3, 3);
			SyncBaseContentCheckBox.Name = "SyncBaseContentCheckBox";
			SyncBaseContentCheckBox.Padding = new Padding(1, 0, 0, 0);
			SyncBaseContentCheckBox.Size = new System.Drawing.Size(122, 17);
			SyncBaseContentCheckBox.TabIndex = 2;
			SyncBaseContentCheckBox.Text = "Sync Base Content";
			SyncBaseContentCheckBox.UseVisualStyleBackColor = true;
			// 
			// PresetNameLabel
			// 
			PresetNameLabel.Anchor = AnchorStyles.Left | AnchorStyles.Right;
			PresetNameLabel.AutoSize = true;
			PresetNameLabel.Location = new System.Drawing.Point(3, 8);
			PresetNameLabel.Name = "PresetNameLabel";
			PresetNameLabel.Size = new System.Drawing.Size(38, 13);
			PresetNameLabel.TabIndex = 0;
			PresetNameLabel.Text = "Preset";
			PresetNameLabel.TextAlign = System.Drawing.ContentAlignment.MiddleRight;
			// 
			// PresetComboBox
			// 
			PresetComboBox.Anchor = AnchorStyles.Left | AnchorStyles.Right;
			PresetComboBox.DropDownStyle = ComboBoxStyle.DropDownList;
			PresetComboBox.FormattingEnabled = true;
			PresetComboBox.Location = new System.Drawing.Point(47, 3);
			PresetComboBox.Name = "PresetComboBox";
			PresetComboBox.Size = new System.Drawing.Size(121, 21);
			PresetComboBox.TabIndex = 1;
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
			SplitContainer.Size = new System.Drawing.Size(1008, 786);
			SplitContainer.SplitterDistance = 462;
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
			tableLayoutPanel1.Size = new System.Drawing.Size(1008, 462);
			tableLayoutPanel1.TabIndex = 8;
			// 
			// groupBox1
			// 
			groupBox1.AutoSize = true;
			groupBox1.Controls.Add(tableLayoutPanel3);
			groupBox1.Dock = DockStyle.Fill;
			groupBox1.Location = new System.Drawing.Point(0, 3);
			groupBox1.Margin = new Padding(0, 3, 0, 3);
			groupBox1.Name = "groupBox1";
			groupBox1.Padding = new Padding(16, 8, 16, 8);
			groupBox1.Size = new System.Drawing.Size(1008, 136);
			groupBox1.TabIndex = 8;
			groupBox1.TabStop = false;
			groupBox1.Text = "General";
			// 
			// tableLayoutPanel3
			// 
			tableLayoutPanel3.AutoSize = true;
			tableLayoutPanel3.ColumnCount = 2;
			tableLayoutPanel3.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50F));
			tableLayoutPanel3.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 50F));
			tableLayoutPanel3.Controls.Add(SyncLocalProjects, 1, 0);
			tableLayoutPanel3.Controls.Add(SyncAllProjects, 0, 0);
			tableLayoutPanel3.Controls.Add(IncludeAllProjectsInSolution, 0, 1);
			tableLayoutPanel3.Controls.Add(GenerateUprojectSpecificSolution, 1, 1);
			tableLayoutPanel3.Controls.Add(flowLayoutPanel3, 0, 2);
			tableLayoutPanel3.Dock = DockStyle.Fill;
			tableLayoutPanel3.Location = new System.Drawing.Point(16, 23);
			tableLayoutPanel3.Name = "tableLayoutPanel3";
			tableLayoutPanel3.RowCount = 3;
			tableLayoutPanel3.RowStyles.Add(new RowStyle(SizeType.Percent, 33.3333321F));
			tableLayoutPanel3.RowStyles.Add(new RowStyle(SizeType.Percent, 33.3333321F));
			tableLayoutPanel3.RowStyles.Add(new RowStyle(SizeType.Percent, 33.3333321F));
			tableLayoutPanel3.Size = new System.Drawing.Size(976, 105);
			tableLayoutPanel3.TabIndex = 96;
			// 
			// SyncLocalProjects
			// 
			SyncLocalProjects.Anchor = AnchorStyles.Left;
			SyncLocalProjects.AutoSize = true;
			SyncLocalProjects.Location = new System.Drawing.Point(491, 8);
			SyncLocalProjects.Name = "SyncLocalProjects";
			SyncLocalProjects.Size = new System.Drawing.Size(120, 17);
			SyncLocalProjects.TabIndex = 9;
			SyncLocalProjects.Text = "Sync local projects";
			SyncLocalProjects.UseVisualStyleBackColor = true;
			// 
			// SyncAllProjects
			// 
			SyncAllProjects.Anchor = AnchorStyles.Left;
			SyncAllProjects.AutoSize = true;
			SyncAllProjects.Location = new System.Drawing.Point(3, 8);
			SyncAllProjects.Name = "SyncAllProjects";
			SyncAllProjects.Size = new System.Drawing.Size(158, 17);
			SyncAllProjects.TabIndex = 6;
			SyncAllProjects.Text = "Sync all projects in stream";
			SyncAllProjects.UseVisualStyleBackColor = true;
			// 
			// IncludeAllProjectsInSolution
			// 
			IncludeAllProjectsInSolution.Anchor = AnchorStyles.Left;
			IncludeAllProjectsInSolution.AutoSize = true;
			IncludeAllProjectsInSolution.Location = new System.Drawing.Point(3, 42);
			IncludeAllProjectsInSolution.Name = "IncludeAllProjectsInSolution";
			IncludeAllProjectsInSolution.Size = new System.Drawing.Size(220, 17);
			IncludeAllProjectsInSolution.TabIndex = 7;
			IncludeAllProjectsInSolution.Text = "Include all synced projects in solution";
			IncludeAllProjectsInSolution.UseVisualStyleBackColor = true;
			// 
			// GenerateUprojectSpecificSolution
			// 
			GenerateUprojectSpecificSolution.Anchor = AnchorStyles.Left;
			GenerateUprojectSpecificSolution.AutoSize = true;
			GenerateUprojectSpecificSolution.Location = new System.Drawing.Point(491, 42);
			GenerateUprojectSpecificSolution.Name = "GenerateUprojectSpecificSolution";
			GenerateUprojectSpecificSolution.Size = new System.Drawing.Size(252, 17);
			GenerateUprojectSpecificSolution.TabIndex = 8;
			GenerateUprojectSpecificSolution.Text = "Generate minimal .uproject specific solution";
			GenerateUprojectSpecificSolution.UseVisualStyleBackColor = true;
			// 
			// flowLayoutPanel3
			// 
			flowLayoutPanel3.AutoSize = true;
			flowLayoutPanel3.Controls.Add(PresetNameLabel);
			flowLayoutPanel3.Controls.Add(PresetComboBox);
			flowLayoutPanel3.Dock = DockStyle.Fill;
			flowLayoutPanel3.Location = new System.Drawing.Point(3, 71);
			flowLayoutPanel3.Name = "flowLayoutPanel3";
			flowLayoutPanel3.Size = new System.Drawing.Size(482, 31);
			flowLayoutPanel3.TabIndex = 10;
			// 
			// CategoriesToolTip
			// 
			CategoriesToolTip.Active = false;
			// 
			// WorkspaceSyncFilterControl
			// 
			AutoScaleDimensions = new System.Drawing.SizeF(96F, 96F);
			AutoScaleMode = AutoScaleMode.Dpi;
			BackColor = System.Drawing.SystemColors.Window;
			Controls.Add(SplitContainer);
			Font = new System.Drawing.Font("Segoe UI", 8.25F);
			Name = "WorkspaceSyncFilterControl";
			Padding = new Padding(7);
			Size = new System.Drawing.Size(1022, 800);
			ViewGroupBox.ResumeLayout(false);
			ViewGroupBox.PerformLayout();
			CustomViewTableLayoutPanel.ResumeLayout(false);
			((System.ComponentModel.ISupportInitialize)ViewDataGrid).EndInit();
			CustomViewsHeaderPanel.ResumeLayout(false);
			CustomViewsHeaderPanel.PerformLayout();
			CategoriesGroupBox.ResumeLayout(false);
			CategoriesTableLayoutPanel.ResumeLayout(false);
			CategoriesTableLayoutPanel.PerformLayout();
			SyncBaseContentFlowLayout.ResumeLayout(false);
			SyncBaseContentFlowLayout.PerformLayout();
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
			flowLayoutPanel3.ResumeLayout(false);
			flowLayoutPanel3.PerformLayout();
			ResumeLayout(false);
		}

		#endregion

		private System.Windows.Forms.GroupBox ViewGroupBox;
		private System.Windows.Forms.GroupBox CategoriesGroupBox;
		private System.Windows.Forms.SplitContainer SplitContainer;
		private System.Windows.Forms.LinkLabel SyntaxButton;
		public System.Windows.Forms.DataGridView ViewDataGrid;
		private System.Windows.Forms.TableLayoutPanel tableLayoutPanel1;
		private System.Windows.Forms.GroupBox groupBox1;
		public System.Windows.Forms.CheckBox SyncAllProjects;
		public System.Windows.Forms.CheckBox IncludeAllProjectsInSolution;
		public System.Windows.Forms.CheckBox GenerateUprojectSpecificSolution;
		private System.Windows.Forms.TableLayoutPanel tableLayoutPanel3;
		private System.Windows.Forms.TableLayoutPanel CustomViewTableLayoutPanel;
		public CheckBox SyncLocalProjects;
		private Label PresetNameLabel;
		public ComboBox PresetComboBox;
		public CheckBox SyncBaseContentCheckBox;
		private FlowLayoutPanel CustomViewsHeaderPanel;
		private Button CopyCustomViewsToClipboard;
		private ToolTip CategoriesToolTip;
		private FlowLayoutPanel flowLayoutPanel3;
		public CheckedListBox CategoriesCheckList;
		private ToolTip SyncBaseContentToolTip;
		private Button PasteCustomViewsToClipboard;
		private Button ClearCustomViewsToClipboard;
		private TableLayoutPanel CategoriesTableLayoutPanel;
		private FlowLayoutPanel SyncBaseContentFlowLayout;
	}
}
